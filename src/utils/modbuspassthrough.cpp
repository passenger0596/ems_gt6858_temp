#include "modbuspassthrough.h"
#include <iostream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <chrono>

ModbusPassthrough::ModbusPassthrough(const std::string& read_port, int read_baudrate,
                                   ForwardMode forward_mode,
                                   const std::string& forward_port,
                                   const std::string& forward_ip,
                                   int slave_id)
    : forward_mode_(forward_mode),
      read_port_(read_port),
      read_baudrate_(read_baudrate),
      forward_port_(forward_port),
      forward_ip_(forward_ip),
      slave_id_(slave_id)
{
    // 创建读取串口上下文
    read_ctx_ = modbus_new_rtu(read_port_.c_str(), read_baudrate_, 'N', 8, 1);
    if (!read_ctx_) {
        std::cerr << "[ERROR] Failed to create read RTU context for " << read_port_ 
                  << ": " << modbus_strerror(errno) << "\n";
        return;
    }
    
    // 设置读取超时
    modbus_set_response_timeout(read_ctx_, 1, 0);      // 1秒响应超时
    modbus_set_byte_timeout(read_ctx_, 0, 100000);     // 100ms字节超时
    
    std::cout << "[INFO] ModbusPassthrough read context created on " << read_port_ 
              << " @ " << read_baudrate_ << " baud\n";
}

ModbusPassthrough::~ModbusPassthrough()
{
    stop();
    
    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    if (forward_thread_.joinable()) {
        forward_thread_.join();
    }
    
    if (read_ctx_) {
        modbus_close(read_ctx_);
        modbus_free(read_ctx_);
        read_ctx_ = nullptr;
    }
    
    if (forward_ctx_) {
        modbus_close(forward_ctx_);
        modbus_free(forward_ctx_);
        forward_ctx_ = nullptr;
    }
    
    if (server_socket_ != -1) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    std::cout << "[INFO] ModbusPassthrough destroyed\n";
}

bool ModbusPassthrough::add_device_mapping(uint8_t device_id, uint16_t start_addr, 
                                          uint16_t count, uint16_t mapped_addr)
{
    if (running_) {
        std::cerr << "[ERROR] Cannot add mapping while running\n";
        return false;
    }
    
    RegisterMapping mapping;
    mapping.device_id = device_id;
    mapping.device_addr = start_addr;
    mapping.count = count;
    mapping.mapped_addr = mapped_addr;
    
    mappings_.push_back(mapping);
    
    std::cout << "[INFO] Added register mapping: Device ID=" << (int)device_id 
              << ", addr[" << start_addr << "-" << (start_addr + count - 1) << "]"
              << " -> Mapped addr[" << mapped_addr << "-" << (mapped_addr + count - 1) << "]\n";
    
    return true;
}

bool ModbusPassthrough::add_coil_mapping(uint8_t device_id, uint16_t start_addr, 
                                        uint16_t count, uint16_t mapped_addr)
{
    if (running_) {
        std::cerr << "[ERROR] Cannot add coil mapping while running\n";
        return false;
    }
    
    CoilMapping mapping;
    mapping.device_id = device_id;
    mapping.device_addr = start_addr;
    mapping.count = count;
    mapping.mapped_addr = mapped_addr;
    
    coil_mappings_.push_back(mapping);
    
    std::cout << "[INFO] Added coil mapping: Device ID=" << (int)device_id 
              << ", addr[" << start_addr << "-" << (start_addr + count - 1) << "]"
              << " -> Mapped addr[" << mapped_addr << "-" << (mapped_addr + count - 1) << "]\n";
    
    return true;
}

bool ModbusPassthrough::add_discrete_input_mapping(uint8_t device_id, uint16_t start_addr, 
                                                  uint16_t count, uint16_t mapped_addr)
{
    if (running_) {
        std::cerr << "[ERROR] Cannot add discrete input mapping while running\n";
        return false;
    }
    
    DiscreteInputMapping mapping;
    mapping.device_id = device_id;
    mapping.device_addr = start_addr;
    mapping.count = count;
    mapping.mapped_addr = mapped_addr;
    
    discrete_input_mappings_.push_back(mapping);
    
    std::cout << "[INFO] Added discrete input mapping: Device ID=" << (int)device_id 
              << ", addr[" << start_addr << "-" << (start_addr + count - 1) << "]"
              << " -> Mapped addr[" << mapped_addr << "-" << (mapped_addr + count - 1) << "]\n";
    
    return true;
}

bool ModbusPassthrough::start()
{
    if (running_) {
        std::cerr << "[ERROR] ModbusPassthrough is already running\n";
        return false;
    }
    
    if (!read_ctx_) {
        std::cerr << "[ERROR] Invalid read context\n";
        return false;
    }
    
    if (mappings_.empty()) {
        std::cerr << "[ERROR] No device mappings configured. Call add_device_mapping() first.\n";
        return false;
    }
    
    // 连接读取串口
    if (modbus_connect(read_ctx_) == -1) {
        std::cerr << "[ERROR] Failed to connect to read port " << read_port_ 
                  << ": " << modbus_strerror(errno) << "\n";
        return false;
    }
    
    std::cout << "[INFO] Connected to read port: " << read_port_ << "\n";
    
    // 创建转发上下文
    if (forward_mode_ == ForwardMode::RTU) {
        // RTU 转发模式
        forward_ctx_ = modbus_new_rtu(forward_port_.c_str(), read_baudrate_, 'N', 8, 1);
        if (!forward_ctx_) {
            std::cerr << "[ERROR] Failed to create forward RTU context: " 
                      << modbus_strerror(errno) << "\n";
            modbus_close(read_ctx_);
            return false;
        }
        
        modbus_set_slave(forward_ctx_, slave_id_);
        modbus_set_response_timeout(forward_ctx_, 1, 0);
        modbus_set_byte_timeout(forward_ctx_, 0, 100000);
        
        if (modbus_connect(forward_ctx_) == -1) {
            std::cerr << "[ERROR] Failed to connect to forward port " << forward_port_ 
                      << ": " << modbus_strerror(errno) << "\n";
            modbus_free(forward_ctx_);
            forward_ctx_ = nullptr;
            modbus_close(read_ctx_);
            return false;
        }
        
        std::cout << "[INFO] Forward RTU mode: " << forward_port_ << " @ slave_id=" << slave_id_ << "\n";
        
    } else {
        // TCP 转发模式
        int port_int = std::stoi(forward_port_);
        forward_ctx_ = modbus_new_tcp(forward_ip_.c_str(), port_int);
        if (!forward_ctx_) {
            std::cerr << "[ERROR] Failed to create forward TCP context: " 
                      << modbus_strerror(errno) << "\n";
            modbus_close(read_ctx_);
            return false;
        }
        
        server_socket_ = modbus_tcp_listen(forward_ctx_, max_connections_);
        if (server_socket_ == -1) {
            std::cerr << "[ERROR] modbus_tcp_listen failed: " << modbus_strerror(errno) << "\n";
            modbus_free(forward_ctx_);
            forward_ctx_ = nullptr;
            modbus_close(read_ctx_);
            return false;
        }
        
        std::cout << "[INFO] Forward TCP mode listening on " 
                  << (forward_ip_.empty() ? "0.0.0.0" : forward_ip_) 
                  << ":" << forward_port_ << "\n";
    }
    
    // 启动线程
    should_stop_ = false;
    running_ = true;
    
    read_thread_ = std::thread(&ModbusPassthrough::read_loop, this);
    forward_thread_ = std::thread(&ModbusPassthrough::forward_loop, this);
    
    std::cout << "[INFO] ModbusPassthrough started successfully\n";
    return true;
}

void ModbusPassthrough::stop()
{
    if (!running_) return;
    
    std::cout << "[INFO] Stopping ModbusPassthrough...\n";
    should_stop_ = true;
    running_ = false;
    
    // 关闭连接
    if (read_ctx_) {
        modbus_close(read_ctx_);
    }
    
    if (forward_ctx_) {
        modbus_close(forward_ctx_);
    }
    
    if (server_socket_ != -1) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    std::cout << "[INFO] ModbusPassthrough stopped\n";
}

void ModbusPassthrough::set_connection_callback(ConnectionCallback callback)
{
    on_connect_ = callback;
}

void ModbusPassthrough::set_disconnection_callback(DisconnectionCallback callback)
{
    on_disconnect_ = callback;
}

// ==================== 内部方法实现 ====================

void ModbusPassthrough::read_loop()
{
    std::cout << "[INFO] Read loop started\n";
    
    while (!should_stop_ && running_) {
        bool all_success = true;
        
        // 轮询所有寄存器映射的设备
        for (const auto& mapping : mappings_) {
            uint16_t data[mapping.count];
            
            if (read_from_device(mapping.device_id, mapping.device_addr, 
                               mapping.count, data)) {
                // 更新缓存
                update_cache(mapping.mapped_addr, mapping.count, data);
            } else {
                std::cerr << "[WARN] Failed to read registers from device ID=" << (int)mapping.device_id 
                         << ", addr=" << mapping.device_addr << "\n";
                all_success = false;
            }
            
            // 短暂延时，避免总线拥堵
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 轮询所有线圈映射的设备
        for (const auto& mapping : coil_mappings_) {
            uint8_t data[mapping.count];
            
            if (read_coils_from_device(mapping.device_id, mapping.device_addr, 
                                      mapping.count, data)) {
                // 更新缓存
                update_coil_cache(mapping.mapped_addr, mapping.count, data);
            } else {
                std::cerr << "[WARN] Failed to read coils from device ID=" << (int)mapping.device_id 
                         << ", addr=" << mapping.device_addr << "\n";
                all_success = false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 轮询所有离散输入映射的设备
        for (const auto& mapping : discrete_input_mappings_) {
            uint8_t data[mapping.count];
            
            if (read_discrete_inputs_from_device(mapping.device_id, mapping.device_addr, 
                                                mapping.count, data)) {
                // 更新缓存
                update_discrete_input_cache(mapping.mapped_addr, mapping.count, data);
            } else {
                std::cerr << "[WARN] Failed to read discrete inputs from device ID=" << (int)mapping.device_id 
                         << ", addr=" << mapping.device_addr << "\n";
                all_success = false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // 所有设备读取完成后，等待一段时间再开始下一轮
        if (all_success) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
    std::cout << "[INFO] Read loop ended\n";
}

void ModbusPassthrough::forward_loop()
{
    std::cout << "[INFO] Forward loop started\n";
    
    if (forward_mode_ == ForwardMode::TCP) {
        // TCP 模式：接受客户端连接
        while (!should_stop_ && running_) {
            if (!forward_ctx_) break;
            
            int client_fd = modbus_tcp_accept(forward_ctx_, &server_socket_);
            if (client_fd == -1) {
                if (should_stop_) break;
                std::cerr << "[WARN] modbus_tcp_accept failed: " << modbus_strerror(errno) << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            if (active_clients_ >= max_connections_) {
                std::cerr << "[WARN] Max clients reached, reject fd=" << client_fd << "\n";
                ::close(client_fd);
                continue;
            }
            
            active_clients_++;
            std::cout << "[INFO] Client connected, fd: " << client_fd << "\n";
            
            if (on_connect_) {
                on_connect_(client_fd);
            }
            
            std::thread client_thread([this, client_fd]() {
                this->handle_client(client_fd);
            });
            client_thread.detach();
        }
    } else {
        // RTU 模式：直接处理请求
        uint8_t req[MODBUS_RTU_MAX_ADU_LENGTH];
        
        while (!should_stop_ && running_) {
            if (!forward_ctx_) break;
            
            int len = modbus_receive(forward_ctx_, req);
            if (len > 0) {
                // 判断功能码
                uint8_t function_code = req[1];  // 功能码在第二个字节
                
                if (function_code == 0x03 || function_code == 0x04) {
                    // 读保持寄存器或读输入寄存器
                    handle_read_request(forward_ctx_, req, len);
                } else if (function_code == 0x06 || function_code == 0x10) {
                    // 写单个寄存器或写多个寄存器
                    handle_write_request(forward_ctx_, req, len);
                } else if (function_code == 0x01) {
                    // 读线圈
                    handle_read_coils_request(forward_ctx_, req, len);
                } else if (function_code == 0x02) {
                    // 读离散输入
                    handle_read_discrete_inputs_request(forward_ctx_, req, len);
                } else if (function_code == 0x05) {
                    // 写单个线圈
                    handle_write_single_coil_request(forward_ctx_, req, len);
                } else if (function_code == 0x0f) {
                    // 写多个线圈
                    handle_write_multiple_coils_request(forward_ctx_, req, len);
                } else {
                    std::cerr << "[WARN] Unsupported function code: 0x" 
                             << std::hex << (int)function_code << std::dec << "\n";
                }
            }
        }
    }
    
    std::cout << "[INFO] Forward loop ended\n";
}

void ModbusPassthrough::handle_client(int client_fd)
{
    std::cout << "[DEBUG] handle_client started for fd=" << client_fd << "\n";
    
    modbus_t* client_ctx = modbus_new_tcp("0.0.0.0", 0);
    if (!client_ctx) {
        std::cerr << "[ERROR] Failed to create client context for fd=" << client_fd << "\n";
        ::close(client_fd);
        active_clients_--;
        return;
    }
    
    if (modbus_set_socket(client_ctx, client_fd) == -1) {
        std::cerr << "[ERROR] Failed to set socket for fd=" << client_fd << "\n";
        modbus_free(client_ctx);
        ::close(client_fd);
        active_clients_--;
        return;
    }
    
    modbus_set_response_timeout(client_ctx, 2, 0);
    modbus_set_byte_timeout(client_ctx, 0, 500000);
    
    uint8_t req[MODBUS_TCP_MAX_ADU_LENGTH];
    
    while (!should_stop_ && running_) {
        if (!client_ctx) break;
        
        int len = modbus_receive(client_ctx, req);
        if (len <= 0) {
            break;
        }
        
        uint8_t function_code = req[7];  // TCP模式下功能码在第8个字节
        
        if (function_code == 0x03 || function_code == 0x04) {
            handle_read_request(client_ctx, req, len);
        } else if (function_code == 0x06 || function_code == 0x10) {
            handle_write_request(client_ctx, req, len);
        } else if (function_code == 0x01) {
            // 读线圈
            handle_read_coils_request(client_ctx, req, len);
        } else if (function_code == 0x02) {
            // 读离散输入
            handle_read_discrete_inputs_request(client_ctx, req, len);
        } else if (function_code == 0x05) {
            // 写单个线圈
            handle_write_single_coil_request(client_ctx, req, len);
        } else if (function_code == 0x0f) {
            // 写多个线圈
            handle_write_multiple_coils_request(client_ctx, req, len);
        } else {
            std::cerr << "[WARN] Unsupported function code: 0x" 
                     << std::hex << (int)function_code << std::dec << "\n";
        }
    }
    
    modbus_close(client_ctx);
    modbus_free(client_ctx);
    
    if (on_disconnect_) {
        on_disconnect_(client_fd);
    }
    
    active_clients_--;
    std::cout << "[INFO] Client disconnected, fd: " << client_fd << "\n";
}

int ModbusPassthrough::handle_read_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0) {
        return -1;
    }
    
    // 解析请求：提取起始地址和数量
    uint16_t start_addr = (req[2] << 8) | req[3];  // 根据Modbus协议，地址在第3、4字节
    uint16_t count = (req[4] << 8) | req[5];       // 数量在第5、6字节
    
    std::cout << "[DEBUG] Read request: addr=" << start_addr << ", count=" << count << "\n";
    
    // 从缓存获取数据
    uint16_t data[count];
    if (get_cached_data(start_addr, count, data)) {
        // 构造响应
        uint8_t rsp[256];
        rsp[0] = slave_id_;  // 从站ID
        rsp[1] = req[1];     // 功能码
        
        if (req[1] == 0x03 || req[1] == 0x04) {
            // 读寄存器响应
            rsp[2] = count * 2;  // 字节数
            
            for (int i = 0; i < count; i++) {
                rsp[3 + i * 2] = (data[i] >> 8) & 0xFF;     // 高字节
                rsp[3 + i * 2 + 1] = data[i] & 0xFF;         // 低字节
            }
            
            int rsp_len = 3 + count * 2;
            
            // 发送响应
            if (modbus_send_raw_request(ctx, rsp, rsp_len) == -1) {
                std::cerr << "[ERROR] Failed to send response: " << modbus_strerror(errno) << "\n";
                return -1;
            }
            
            return rsp_len;
        }
    } else {
        std::cerr << "[ERROR] Cached data not found for addr=" << start_addr << "\n";
        return -1;
    }
    
    return -1;
}

int ModbusPassthrough::handle_write_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0) {
        return -1;
    }
    
    uint8_t function_code = req[1];
    uint16_t start_addr = (req[2] << 8) | req[3];
    
    std::cout << "[DEBUG] Write request: function=0x" << std::hex << (int)function_code 
              << std::dec << ", addr=" << start_addr << "\n";
    
    // 查找对应的设备映射
    for (const auto& mapping : mappings_) {
        if (start_addr >= mapping.mapped_addr && 
            start_addr < mapping.mapped_addr + mapping.count) {
            
            // 计算在原始设备中的地址
            uint16_t offset = start_addr - mapping.mapped_addr;
            uint16_t device_addr = mapping.device_addr + offset;
            
            if (function_code == 0x06) {
                // 写单个寄存器
                uint16_t value = (req[4] << 8) | req[5];
                
                std::cout << "[INFO] Forwarding write to device ID=" << (int)mapping.device_id 
                         << ", addr=" << device_addr << ", value=" << value << "\n";
                
                if (write_to_device(mapping.device_id, device_addr, 1, &value)) {
                    // 更新缓存
                    update_cache(start_addr, 1, &value);
                    
                    // 发送确认响应（原样返回请求）
                    if (modbus_send_raw_request(ctx, req, req_length) == -1) {
                        std::cerr << "[ERROR] Failed to send write confirmation\n";
                        return -1;
                    }
                    return req_length;
                } else {
                    std::cerr << "[ERROR] Failed to write to device\n";
                    return -1;
                }
                
            } else if (function_code == 0x10) {
                // 写多个寄存器
                uint16_t count = (req[4] << 8) | req[5];
                uint8_t byte_count = req[6];
                
                if (byte_count != count * 2) {
                    std::cerr << "[ERROR] Byte count mismatch\n";
                    return -1;
                }
                
                uint16_t data[count];
                for (int i = 0; i < count; i++) {
                    data[i] = (req[7 + i * 2] << 8) | req[7 + i * 2 + 1];
                }
                
                std::cout << "[INFO] Forwarding write to device ID=" << (int)mapping.device_id 
                         << ", addr=" << device_addr << ", count=" << count << "\n";
                
                if (write_to_device(mapping.device_id, device_addr, count, data)) {
                    // 更新缓存
                    update_cache(start_addr, count, data);
                    
                    // 发送确认响应
                    uint8_t rsp[12];
                    rsp[0] = slave_id_;
                    rsp[1] = 0x10;
                    rsp[2] = req[2];  // 起始地址高字节
                    rsp[3] = req[3];  // 起始地址低字节
                    rsp[4] = req[4];  // 数量高字节
                    rsp[5] = req[5];  // 数量低字节
                    
                    if (modbus_send_raw_request(ctx, rsp, 6) == -1) {
                        std::cerr << "[ERROR] Failed to send write confirmation\n";
                        return -1;
                    }
                    return 6;
                } else {
                    std::cerr << "[ERROR] Failed to write to device\n";
                    return -1;
                }
            }
        }
    }
    
    std::cerr << "[ERROR] No mapping found for address " << start_addr << "\n";
    return -1;
}

bool ModbusPassthrough::read_from_device(uint8_t device_id, uint16_t addr, 
                                        uint16_t count, uint16_t* data)
{
    if (!read_ctx_) {
        return false;
    }
    
    // 设置从站ID
    modbus_set_slave(read_ctx_, device_id);
    
    // 读取保持寄存器
    int rc = modbus_read_registers(read_ctx_, addr, count, data);
    
    if (rc == count) {
        return true;
    } else {
        std::cerr << "[ERROR] Read failed: device_id=" << (int)device_id 
                 << ", addr=" << addr << ", count=" << count 
                 << ", error: " << modbus_strerror(errno) << "\n";
        return false;
    }
}

bool ModbusPassthrough::write_to_device(uint8_t device_id, uint16_t addr, 
                                       uint16_t count, const uint16_t* data)
{
    if (!read_ctx_) {
        return false;
    }
    
    // 设置从站ID
    modbus_set_slave(read_ctx_, device_id);
    
    int rc;
    if (count == 1) {
        // 写单个寄存器
        rc = modbus_write_register(read_ctx_, addr, data[0]);
    } else {
        // 写多个寄存器
        rc = modbus_write_registers(read_ctx_, addr, count, data);
    }
    
    if (rc == count || (count == 1 && rc == 1)) {
        return true;
    } else {
        std::cerr << "[ERROR] Write failed: device_id=" << (int)device_id 
                 << ", addr=" << addr << ", count=" << count 
                 << ", error: " << modbus_strerror(errno) << "\n";
        return false;
    }
}

void ModbusPassthrough::update_cache(uint16_t mapped_addr, uint16_t count, const uint16_t* data)
{
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    for (uint16_t i = 0; i < count; i++) {
        data_cache_[mapped_addr + i] = data[i];
    }
}

bool ModbusPassthrough::get_cached_data(uint16_t mapped_addr, uint16_t count, uint16_t* data)
{
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    for (uint16_t i = 0; i < count; i++) {
        auto it = data_cache_.find(mapped_addr + i);
        if (it == data_cache_.end()) {
            return false;
        }
        data[i] = it->second;
    }
    
    return true;
}

// ==================== 线圈和离散输入处理函数 ====================

int ModbusPassthrough::handle_read_coils_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0) {
        return -1;
    }
    
    // 解析请求：提取起始地址和数量
    uint16_t start_addr = (req[2] << 8) | req[3];
    uint16_t count = (req[4] << 8) | req[5];
    
    std::cout << "[DEBUG] Read coils request: addr=" << start_addr << ", count=" << count << "\n";
    
    // 从缓存获取数据
    uint8_t data[count];
    if (get_cached_coils(start_addr, count, data)) {
        // 构造响应
        uint8_t rsp[256];
        rsp[0] = slave_id_;  // 从站ID
        rsp[1] = 0x01;       // 功能码
        
        // 计算字节数（每8个线圈占1个字节）
        uint8_t byte_count = (count + 7) / 8;
        rsp[2] = byte_count;
        
        // 复制数据
        std::memcpy(&rsp[3], data, byte_count);
        
        int rsp_len = 3 + byte_count;
        
        // 发送响应
        if (modbus_send_raw_request(ctx, rsp, rsp_len) == -1) {
            std::cerr << "[ERROR] Failed to send response: " << modbus_strerror(errno) << "\n";
            return -1;
        }
        
        return rsp_len;
    } else {
        std::cerr << "[ERROR] Cached coil data not found for addr=" << start_addr << "\n";
        return -1;
    }
}

int ModbusPassthrough::handle_read_discrete_inputs_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0) {
        return -1;
    }
    
    // 解析请求：提取起始地址和数量
    uint16_t start_addr = (req[2] << 8) | req[3];
    uint16_t count = (req[4] << 8) | req[5];
    
    std::cout << "[DEBUG] Read discrete inputs request: addr=" << start_addr << ", count=" << count << "\n";
    
    // 从缓存获取数据
    uint8_t data[count];
    if (get_cached_discrete_inputs(start_addr, count, data)) {
        // 构造响应
        uint8_t rsp[256];
        rsp[0] = slave_id_;  // 从站ID
        rsp[1] = 0x02;       // 功能码
        
        // 计算字节数（每8个离散输入占1个字节）
        uint8_t byte_count = (count + 7) / 8;
        rsp[2] = byte_count;
        
        // 复制数据
        std::memcpy(&rsp[3], data, byte_count);
        
        int rsp_len = 3 + byte_count;
        
        // 发送响应
        if (modbus_send_raw_request(ctx, rsp, rsp_len) == -1) {
            std::cerr << "[ERROR] Failed to send response: " << modbus_strerror(errno) << "\n";
            return -1;
        }
        
        return rsp_len;
    } else {
        std::cerr << "[ERROR] Cached discrete input data not found for addr=" << start_addr << "\n";
        return -1;
    }
}

int ModbusPassthrough::handle_write_single_coil_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0) {
        return -1;
    }
    
    uint16_t start_addr = (req[2] << 8) | req[3];
    uint16_t value = (req[4] << 8) | req[5];
    bool coil_value = (value == 0xFF00);  // Modbus规定：0xFF00=ON, 0x0000=OFF
    
    std::cout << "[DEBUG] Write single coil request: addr=" << start_addr 
              << ", value=" << (coil_value ? "ON" : "OFF") << "\n";
    
    // 查找对应的设备映射
    for (const auto& mapping : coil_mappings_) {
        if (start_addr >= mapping.mapped_addr && 
            start_addr < mapping.mapped_addr + mapping.count) {
            
            // 计算在原始设备中的地址
            uint16_t offset = start_addr - mapping.mapped_addr;
            uint16_t device_addr = mapping.device_addr + offset;
            
            std::cout << "[INFO] Forwarding write coil to device ID=" << (int)mapping.device_id 
                     << ", addr=" << device_addr << ", value=" << (coil_value ? "ON" : "OFF") << "\n";
            
            if (write_single_coil_to_device(mapping.device_id, device_addr, coil_value)) {
                // 更新缓存
                uint8_t cache_value = coil_value ? 1 : 0;
                update_coil_cache(start_addr, 1, &cache_value);
                
                // 发送确认响应（原样返回请求）
                if (modbus_send_raw_request(ctx, req, req_length) == -1) {
                    std::cerr << "[ERROR] Failed to send write confirmation\n";
                    return -1;
                }
                return req_length;
            } else {
                std::cerr << "[ERROR] Failed to write coil to device\n";
                return -1;
            }
        }
    }
    
    std::cerr << "[ERROR] No coil mapping found for address " << start_addr << "\n";
    return -1;
}

int ModbusPassthrough::handle_write_multiple_coils_request(modbus_t* ctx, uint8_t* req, int req_length)
{
    if (req_length <= 0) {
        return -1;
    }
    
    uint16_t start_addr = (req[2] << 8) | req[3];
    uint16_t count = (req[4] << 8) | req[5];
    uint8_t byte_count = req[6];
    
    std::cout << "[DEBUG] Write multiple coils request: addr=" << start_addr 
              << ", count=" << count << ", bytes=" << (int)byte_count << "\n";
    
    // 查找对应的设备映射
    for (const auto& mapping : coil_mappings_) {
        if (start_addr >= mapping.mapped_addr && 
            start_addr < mapping.mapped_addr + mapping.count) {
            
            // 计算在原始设备中的地址
            uint16_t offset = start_addr - mapping.mapped_addr;
            uint16_t device_addr = mapping.device_addr + offset;
            
            const uint8_t* data = &req[7];
            
            std::cout << "[INFO] Forwarding write multiple coils to device ID=" << (int)mapping.device_id 
                     << ", addr=" << device_addr << ", count=" << count << "\n";
            
            if (write_multiple_coils_to_device(mapping.device_id, device_addr, count, data)) {
                // 更新缓存
                update_coil_cache(start_addr, count, data);
                
                // 发送确认响应
                uint8_t rsp[12];
                rsp[0] = slave_id_;
                rsp[1] = 0x0f;
                rsp[2] = req[2];  // 起始地址高字节
                rsp[3] = req[3];  // 起始地址低字节
                rsp[4] = req[4];  // 数量高字节
                rsp[5] = req[5];  // 数量低字节
                
                if (modbus_send_raw_request(ctx, rsp, 6) == -1) {
                    std::cerr << "[ERROR] Failed to send write confirmation\n";
                    return -1;
                }
                return 6;
            } else {
                std::cerr << "[ERROR] Failed to write multiple coils to device\n";
                return -1;
            }
        }
    }
    
    std::cerr << "[ERROR] No coil mapping found for address " << start_addr << "\n";
    return -1;
}

// ==================== 线圈和离散输入的读写函数 ====================

bool ModbusPassthrough::read_coils_from_device(uint8_t device_id, uint16_t addr, uint16_t count, uint8_t* data)
{
    if (!read_ctx_) {
        return false;
    }
    
    // 设置从站ID
    modbus_set_slave(read_ctx_, device_id);
    
    // 读取线圈
    int rc = modbus_read_bits(read_ctx_, addr, count, data);
    
    if (rc == count) {
        return true;
    } else {
        std::cerr << "[ERROR] Read coils failed: device_id=" << (int)device_id 
                 << ", addr=" << addr << ", count=" << count 
                 << ", error: " << modbus_strerror(errno) << "\n";
        return false;
    }
}

bool ModbusPassthrough::read_discrete_inputs_from_device(uint8_t device_id, uint16_t addr, uint16_t count, uint8_t* data)
{
    if (!read_ctx_) {
        return false;
    }
    
    // 设置从站ID
    modbus_set_slave(read_ctx_, device_id);
    
    // 读取离散输入
    int rc = modbus_read_input_bits(read_ctx_, addr, count, data);
    
    if (rc == count) {
        return true;
    } else {
        std::cerr << "[ERROR] Read discrete inputs failed: device_id=" << (int)device_id 
                 << ", addr=" << addr << ", count=" << count 
                 << ", error: " << modbus_strerror(errno) << "\n";
        return false;
    }
}

bool ModbusPassthrough::write_single_coil_to_device(uint8_t device_id, uint16_t addr, bool value)
{
    if (!read_ctx_) {
        return false;
    }
    
    // 设置从站ID
    modbus_set_slave(read_ctx_, device_id);
    
    // 写单个线圈
    int rc = modbus_write_bit(read_ctx_, addr, value ? 1 : 0);
    
    if (rc == 1) {
        return true;
    } else {
        std::cerr << "[ERROR] Write single coil failed: device_id=" << (int)device_id 
                 << ", addr=" << addr << ", value=" << (value ? "ON" : "OFF")
                 << ", error: " << modbus_strerror(errno) << "\n";
        return false;
    }
}

bool ModbusPassthrough::write_multiple_coils_to_device(uint8_t device_id, uint16_t addr, uint16_t count, const uint8_t* data)
{
    if (!read_ctx_) {
        return false;
    }
    
    // 设置从站ID
    modbus_set_slave(read_ctx_, device_id);
    
    // 写多个线圈
    int rc = modbus_write_bits(read_ctx_, addr, count, data);
    
    if (rc == count) {
        return true;
    } else {
        std::cerr << "[ERROR] Write multiple coils failed: device_id=" << (int)device_id 
                 << ", addr=" << addr << ", count=" << count 
                 << ", error: " << modbus_strerror(errno) << "\n";
        return false;
    }
}

// ==================== 线圈和离散输入的缓存管理 ====================

void ModbusPassthrough::update_coil_cache(uint16_t mapped_addr, uint16_t count, const uint8_t* data)
{
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    for (uint16_t i = 0; i < count; i++) {
        coil_cache_[mapped_addr + i] = data[i];
    }
}

bool ModbusPassthrough::get_cached_coils(uint16_t mapped_addr, uint16_t count, uint8_t* data)
{
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    for (uint16_t i = 0; i < count; i++) {
        auto it = coil_cache_.find(mapped_addr + i);
        if (it == coil_cache_.end()) {
            return false;
        }
        data[i] = it->second;
    }
    
    return true;
}

void ModbusPassthrough::update_discrete_input_cache(uint16_t mapped_addr, uint16_t count, const uint8_t* data)
{
    std::unique_lock<std::shared_mutex> lock(cache_mutex_);
    
    for (uint16_t i = 0; i < count; i++) {
        discrete_input_cache_[mapped_addr + i] = data[i];
    }
}

bool ModbusPassthrough::get_cached_discrete_inputs(uint16_t mapped_addr, uint16_t count, uint8_t* data)
{
    std::shared_lock<std::shared_mutex> lock(cache_mutex_);
    
    for (uint16_t i = 0; i < count; i++) {
        auto it = discrete_input_cache_.find(mapped_addr + i);
        if (it == discrete_input_cache_.end()) {
            return false;
        }
        data[i] = it->second;
    }
    
    return true;
}
