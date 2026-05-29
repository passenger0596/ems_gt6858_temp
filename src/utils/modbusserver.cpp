#include "modbusserver.h"
#include <thread>
#include <chrono>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "device.h"
#include "log.h"


// ==================== 构造函数 ====================

ModbusServer::ModbusServer(const std::string& port, int baudrate, int slave_id)
    : server_socket_(-1), ip_(""), port_(port), baudrate_(baudrate), max_connections_(0), is_tcp_(false)
{
    ctx_ = modbus_new_rtu(port_.c_str(), baudrate_, 'N', 8, 1);
    if (!ctx_) {
        LOG_ERROR_LOC("Failed to create Modbus RTU context: " + std::string(modbus_strerror(errno)));
        return;
    }

    modbus_set_slave(ctx_, slave_id);

    modbus_set_response_timeout(ctx_, 1, 0);
    modbus_set_byte_timeout(ctx_, 0, 100000);

    LOG_INFO_LOC("ModbusServer RTU created on port: " + port_ +
                 ", baudrate: " + std::to_string(baudrate_) +
                 ", slave_id: " + std::to_string(slave_id));
}


ModbusServer::ModbusServer(const std::string& ip, const std::string& port, int max_connections)
    : server_socket_(-1), ip_(ip), port_(port), baudrate_(0), max_connections_(max_connections), is_tcp_(true)
{
    int port_int = std::stoi(port_);
    ctx_ = modbus_new_tcp(ip_.c_str(), port_int);

    if (!ctx_) {
        LOG_ERROR_LOC("Failed to create Modbus TCP context: " + std::string(modbus_strerror(errno)));
        return;
    }

    LOG_INFO_LOC("ModbusServer TCP initialized on " + ip_ + ":" + port_);
}

ModbusServer::~ModbusServer()
{
    stop();
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }

    if (mb_mapping_) {
        modbus_mapping_free(mb_mapping_);
        mb_mapping_ = nullptr;
    }

    if (ctx_) {
        modbus_close(ctx_);
        modbus_free(ctx_);
        ctx_ = nullptr;
    }

    LOG_INFO_LOC("ModbusServer destroyed");
}


// ==================== 服务器控制 ====================

bool ModbusServer::start()
{
    if (running_) {
        LOG_ERROR_LOC("ModbusServer is already running");
        return false;
    }

    if (!ctx_) {
        LOG_ERROR_LOC("Invalid modbus context");
        return false;
    }

    if (!mb_mapping_) {
        LOG_WARNING_LOC("Data area not initialized! Call init_data_area() before start(). "
                        "Using default data area size (100 coils/bits/holding/input regs)");
        init_data_area();
    }

    if (is_tcp_) {
        server_socket_ = modbus_tcp_listen(ctx_, max_connections_);
        if (server_socket_ == -1) {
            LOG_ERROR_LOC("modbus_tcp_listen failed: " + std::string(modbus_strerror(errno)));
            return false;
        }
        LOG_INFO_LOC("ModbusServer TCP listening on port " + port_ + " (listen_fd=" +
                     std::to_string(server_socket_) + ")");
    } else {
        if (modbus_connect(ctx_) == -1) {
            LOG_ERROR_LOC("modbus_connect failed: " + std::string(modbus_strerror(errno)));
            return false;
        }
    }

    running_ = true;
    should_stop_ = false;
    listen_thread_ = std::thread(&ModbusServer::listen_loop, this);
    return true;
}


void ModbusServer::stop()
{
    if (!running_) return;

    LOG_INFO_LOC("Stopping ModbusServer...");
    should_stop_ = true;
    running_ = false;

    // 先 shutdown 打断 listen_loop 中的阻塞 accept()
    if (is_tcp_ && server_socket_ >= 0) {
        shutdown(server_socket_, SHUT_RDWR);
    }

    if (ctx_) {
        modbus_close(ctx_);
        server_socket_ = -1;
    }

    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }

    LOG_INFO_LOC("ModbusServer stopped");
}


bool ModbusServer::set_slave_id(int slave_id)
{
    if (is_tcp_) {
        LOG_WARNING_LOC("Cannot set slave ID in TCP mode");
        return false;
    }

    return modbus_set_slave(ctx_, slave_id) != -1;
}


// ==================== 数据区操作 ====================

void ModbusServer::init_data_area(int num_coils,
                                  int num_input_bits,
                                  int num_holding_regs,
                                  int num_input_regs)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (mb_mapping_) {
        modbus_mapping_free(mb_mapping_);
    }

    mb_mapping_ = modbus_mapping_new(num_coils, num_input_bits,
                                     num_holding_regs, num_input_regs);

    if (!mb_mapping_) {
        LOG_ERROR_LOC("Failed to allocate modbus mapping");
        return;
    }

    LOG_INFO_LOC("Data area initialized: coils=" + std::to_string(num_coils) +
                 ", input_bits=" + std::to_string(num_input_bits) +
                 ", holding_regs=" + std::to_string(num_holding_regs) +
                 ", input_regs=" + std::to_string(num_input_regs));
}


bool ModbusServer::set_coil(int addr, bool value)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_bits) {
        LOG_ERROR_LOC("Coil address out of range: " + std::to_string(addr));
        return false;
    }

    mb_mapping_->tab_bits[addr] = value ? 1 : 0;
    return true;
}


bool ModbusServer::set_coils(int addr, int count, const uint8_t* values)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_bits) {
        LOG_ERROR_LOC("Coil addresses out of range");
        return false;
    }

    std::memcpy(&mb_mapping_->tab_bits[addr], values, count);
    return true;
}


bool ModbusServer::set_input_bit(int addr, bool value)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_input_bits) {
        LOG_ERROR_LOC("Input bit address out of range: " + std::to_string(addr));
        return false;
    }

    mb_mapping_->tab_input_bits[addr] = value ? 1 : 0;
    return true;
}


bool ModbusServer::set_input_bits(int addr, int count, const uint8_t* values)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_input_bits) {
        LOG_ERROR_LOC("Input bit addresses out of range");
        return false;
    }

    std::memcpy(&mb_mapping_->tab_input_bits[addr], values, count);
    return true;
}


bool ModbusServer::set_holding_register(int addr, uint16_t value)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_registers) {
        LOG_ERROR_LOC("Holding register address out of range: " + std::to_string(addr));
        return false;
    }

    mb_mapping_->tab_registers[addr] = value;
    return true;
}


bool ModbusServer::set_holding_registers(int addr, int count, const uint16_t* values)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_registers) {
        LOG_ERROR_LOC("Holding register addresses out of range");
        return false;
    }

    std::memcpy(&mb_mapping_->tab_registers[addr], values, count * sizeof(uint16_t));
    return true;
}


bool ModbusServer::set_input_register(int addr, uint16_t value)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_input_registers) {
        LOG_ERROR_LOC("Input register address out of range: " + std::to_string(addr));
        return false;
    }

    mb_mapping_->tab_input_registers[addr] = value;
    return true;
}


bool ModbusServer::set_input_registers(int addr, int count, const uint16_t* values)
{
    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_input_registers) {
        LOG_ERROR_LOC("Input register addresses out of range");
        return false;
    }

    std::memcpy(&mb_mapping_->tab_input_registers[addr], values, count * sizeof(uint16_t));
    return true;
}


// ==================== 获取数据区值 ====================

bool ModbusServer::get_coil(int addr, bool* value) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_bits) {
        return false;
    }

    *value = mb_mapping_->tab_bits[addr] != 0;
    return true;
}


bool ModbusServer::get_input_bit(int addr, bool* value) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_input_bits) {
        return false;
    }

    *value = mb_mapping_->tab_input_bits[addr] != 0;
    return true;
}


bool ModbusServer::get_holding_register(int addr, uint16_t* value) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_registers) {
        return false;
    }

    *value = mb_mapping_->tab_registers[addr];
    return true;
}


bool ModbusServer::get_input_register(int addr, uint16_t* value) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr >= mb_mapping_->nb_input_registers) {
        return false;
    }

    *value = mb_mapping_->tab_input_registers[addr];
    return true;
}


std::vector<uint8_t> ModbusServer::get_coils(int addr, int count) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_bits) {
        return {};
    }

    return std::vector<uint8_t>(mb_mapping_->tab_bits + addr,
                                mb_mapping_->tab_bits + addr + count);
}


std::vector<uint8_t> ModbusServer::get_input_bits(int addr, int count) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_input_bits) {
        return {};
    }

    return std::vector<uint8_t>(mb_mapping_->tab_input_bits + addr,
                                mb_mapping_->tab_input_bits + addr + count);
}


std::vector<uint16_t> ModbusServer::get_holding_registers(int addr, int count) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_registers) {
        return {};
    }

    return std::vector<uint16_t>(mb_mapping_->tab_registers + addr,
                                 mb_mapping_->tab_registers + addr + count);
}


std::vector<uint16_t> ModbusServer::get_input_registers(int addr, int count) const
{
    std::shared_lock<std::shared_mutex> lock(data_mutex_);

    if (!mb_mapping_ || addr < 0 || addr + count > mb_mapping_->nb_input_registers) {
        return {};
    }

    return std::vector<uint16_t>(mb_mapping_->tab_input_registers + addr,
                                 mb_mapping_->tab_input_registers + addr + count);
}


// ==================== 回调设置 ====================

void ModbusServer::set_connection_callback(ConnectionCallback callback)
{
    on_connect_ = callback;
}


void ModbusServer::set_disconnection_callback(DisconnectionCallback callback)
{
    on_disconnect_ = callback;
}


// ==================== 内部方法 ====================

void ModbusServer::listen_loop()
{
    LOG_INFO_LOC("ModbusServer listen loop started");
    LOG_DEBUG_LOC("is_tcp_=" + std::to_string(is_tcp_) +
                  ", ctx_=" + std::to_string(reinterpret_cast<uintptr_t>(ctx_)) +
                  ", mb_mapping_=" + std::to_string(reinterpret_cast<uintptr_t>(mb_mapping_)));

    if (is_tcp_) {
        while (!should_stop_ && running_) {
            LOG_DEBUG_LOC("Waiting for client connection...");

            if (!ctx_) {
                LOG_ERROR_LOC("ctx_ is null, exit listen loop");
                break;
            }
            int client_fd = modbus_tcp_accept(ctx_, &server_socket_);
            if (client_fd == -1) {
                if (should_stop_) break;
                LOG_ERROR_LOC("modbus_tcp_accept failed: " + std::string(modbus_strerror(errno)));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            LOG_DEBUG_LOC("Client accepted, fd=" + std::to_string(client_fd));

            if (active_clients_ >= max_connections_) {
                LOG_WARNING_LOC("Max clients reached (" + std::to_string(max_connections_) +
                                "), reject fd=" + std::to_string(client_fd));
                ::close(client_fd);
                continue;
            }

            active_clients_++;
            LOG_INFO_LOC("Client connected, fd: " + std::to_string(client_fd));

            if (on_connect_) {
                LOG_DEBUG_LOC("Calling on_connect callback");
                on_connect_(client_fd);
            }

            std::thread client_thread([this, client_fd]() {
                this->handle_client(client_fd);
            });
            client_thread.detach();
            LOG_DEBUG_LOC("Client thread detached");
        }
    } else {
        // RTU 模式下，直接处理接收的请求
        while (!should_stop_ && running_) {
            uint8_t req[MODBUS_RTU_MAX_ADU_LENGTH];
            int len = modbus_receive(ctx_, req);
            if (len > 0) {
                handle_request(ctx_, req, len);
            }
        }
    }

    LOG_INFO_LOC("ModbusServer listen loop ended");
}


void ModbusServer::handle_client(int client_fd)
{
    LOG_DEBUG_LOC("handle_client started for fd=" + std::to_string(client_fd));

    modbus_t* client_ctx = modbus_new_tcp("0.0.0.0", 0);
    if (!client_ctx) {
        LOG_ERROR_LOC("Failed to create client context for fd=" + std::to_string(client_fd) +
                      ", errno=" + std::to_string(errno) + ": " + strerror(errno));
        ::close(client_fd);
        active_clients_--;
        return;
    }

    LOG_DEBUG_LOC("client_ctx created: " + std::to_string(reinterpret_cast<uintptr_t>(client_ctx)));

    int rc = modbus_set_socket(client_ctx, client_fd);
    if (rc == -1) {
        LOG_ERROR_LOC("Failed to set socket for fd=" + std::to_string(client_fd) +
                      ": " + modbus_strerror(errno));
        modbus_free(client_ctx);
        ::close(client_fd);
        return;
    }

    LOG_DEBUG_LOC("Socket set successfully");

    // 优化超时设置：增加响应超时时间以应对锁竞争
    modbus_set_response_timeout(client_ctx, 5, 0);  // 从2秒增加到5秒
    modbus_set_byte_timeout(client_ctx, 1, 0);      // 从500ms增加到1秒

    uint8_t req[MODBUS_TCP_MAX_ADU_LENGTH];

    LOG_DEBUG_LOC("Entering receive loop");
    while (!should_stop_ && running_) {
        if (!client_ctx) break;

        int len = modbus_receive(client_ctx, req);
        if (len <= 0) {
            if (len == -1) {
                LOG_DEBUG_LOC("modbus_receive error: " + std::string(modbus_strerror(errno)));
            }
            LOG_DEBUG_LOC("modbus_receive returned " + std::to_string(len) + ", breaking");
            break;
        }

        LOG_DEBUG_LOC("Received " + std::to_string(len) + " bytes, calling handle_request");
        handle_request(client_ctx, req, len);
    }

    LOG_DEBUG_LOC("Cleaning up client fd=" + std::to_string(client_fd));
    modbus_close(client_ctx);
    modbus_free(client_ctx);

    if (on_disconnect_) {
        on_disconnect_(client_fd);
    }

    active_clients_--;
    LOG_INFO_LOC("Client disconnected, fd: " + std::to_string(client_fd) +
                 ", active clients: " + std::to_string(active_clients_.load()));
}


int ModbusServer::handle_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0 || !mb_mapping_) {
        return -1;
    }

    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    int rc = modbus_reply(ctx, req, req_length, mb_mapping_);
    if (rc == -1) {
        LOG_ERROR_LOC("Failed to reply: " + std::string(modbus_strerror(errno)));
    }
    return rc;
}


// ==================== 设备数据映射 ====================

int ModbusServer::map_device_to_input_registers(std::shared_ptr<Device> device, uint16_t start_addr)
{
    if (!device) {
        LOG_ERROR_LOC("Invalid device pointer");
        return 0;
    }

    if (!mb_mapping_) {
        LOG_ERROR_LOC("Modbus mapping not initialized. Call init_data_area() first.");
        return 0;
    }

    const auto& data_dict = device->data_dict_;
    const auto& dev_data_keys = device->dev_data_keys_;

    if (data_dict.empty() || dev_data_keys.empty()) {
        LOG_WARNING_LOC("Device '" + device->get_name() + "' has no data dictionary");
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(data_mutex_);

    uint16_t current_addr = start_addr;
    int mapped_count = 0;

    LOG_INFO_LOC("Mapping device '" + device->get_name() +
                 "' to input registers starting at address " + std::to_string(start_addr));

    for (const auto& key : dev_data_keys) {
        auto it = data_dict.find(key);
        if (it == data_dict.end()) {
            LOG_WARNING_LOC("Key '" + key + "' not found in data_dict");
            continue;
        }

        const RegisterData& reg_data = it->second;

        if (reg_data.datatype.find("INT32") != std::string::npos) {
            if (current_addr + 1 >= mb_mapping_->nb_input_registers) {
                LOG_ERROR_LOC("Not enough input registers for INT32 data at address " +
                              std::to_string(current_addr));
                break;
            }

            int32_t int32_value = static_cast<int32_t>((reg_data.value - reg_data.offset) * reg_data.mag);

            uint16_t high_word = static_cast<uint16_t>((int32_value >> 16) & 0xFFFF);
            uint16_t low_word = static_cast<uint16_t>(int32_value & 0xFFFF);

            mb_mapping_->tab_input_registers[current_addr] = high_word;
            mb_mapping_->tab_input_registers[current_addr + 1] = low_word;

            LOG_DEBUG_LOC("  [INT32] " + key + " -> addr[" + std::to_string(current_addr) +
                          "," + std::to_string(current_addr + 1) + "] = " + std::to_string(int32_value));

            current_addr += 2;
            mapped_count += 2;

        } else {
            if (current_addr >= mb_mapping_->nb_input_registers) {
                LOG_ERROR_LOC("Not enough input registers at address " + std::to_string(current_addr));
                break;
            }

            int16_t int16_value = static_cast<int16_t>((reg_data.value - reg_data.offset) * reg_data.mag);

            mb_mapping_->tab_input_registers[current_addr] = static_cast<uint16_t>(int16_value);

            LOG_DEBUG_LOC("  [INT16] " + key + " -> addr[" + std::to_string(current_addr) +
                          "] = " + std::to_string(int16_value));

            current_addr += 1;
            mapped_count += 1;
        }
    }

    LOG_INFO_LOC("Total mapped " + std::to_string(mapped_count) + " registers for device '" +
                 device->get_name() + "'");

    return mapped_count;
}
