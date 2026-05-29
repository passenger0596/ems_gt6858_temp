#include "modbusclient.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include "log.h"

ModbusClient::ModbusClient(const std::string& port, const int& baudrate)
: ip_(""), port_(port), baudrate_(baudrate)  // 串口通信，ip设为空字符串
{
    ctx_ = modbus_new_rtu(port_.c_str(), baudrate_, 'N', 8, 1);
    if (!ctx_) {
        LOG_ERROR_LOC(("Failed to create RTU context for port: " + port_).c_str());
        return;
    }
    
    LOG_INFO_LOC(("RTU context created successfully for " + port_).c_str());
    
    // 设置相应超时和字节超时
    modbus_set_response_timeout(ctx_, 1, 0);
    modbus_set_byte_timeout(ctx_, 0, 100000);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}


ModbusClient::ModbusClient(const std::string& ip, const std::string& port)
: ip_(ip), port_(port), baudrate_(0)  // TCP连接不需要波特率，设为0
{
    ctx_ = modbus_new_tcp(ip_.c_str(), std::stoi(port_));  // port需要转换为int
}

// modbus连接方法
bool ModbusClient::connect()
{
    if (!ctx_) {
        LOG_ERROR_LOC("Modbus context is null, cannot connect");
        this->connected_ = false;
        return false;
    }
    
    // 对于RTU串口，添加更详细的错误诊断
    if (ip_.empty()) {
        
        // 检查串口设备文件是否存在和可访问
        struct stat st;
        if (stat(port_.c_str(), &st) != 0) {
            LOG_ERROR_LOC(("Serial port device does not exist or cannot be accessed: " + port_ + ", errno: " + std::to_string(errno)).c_str());
            this->connected_ = false;
            return false;
        }
        
        // 检查是否为字符设备
        if (!S_ISCHR(st.st_mode)) {
            LOG_ERROR_LOC(("Serial port is not a character device: " + port_).c_str());
            this->connected_ = false;
            return false;
        }
        
        // 检查权限
        if (access(port_.c_str(), R_OK | W_OK) != 0) {
            LOG_ERROR_LOC(("No read/write permission for serial port: " + port_ + ", errno: " + std::to_string(errno)).c_str());
            this->connected_ = false;
            return false;
        }
        
    }
    
    if (modbus_connect(ctx_) == -1) {
        std::string error_msg = modbus_strerror(errno);
        LOG_ERROR_LOC(("Connection failed for " + port_ + ": " + error_msg + " (errno: " + std::to_string(errno) + ")").c_str());
        
        // 提供更多调试信息
        if (errno == EINVAL) {
            LOG_ERROR_LOC("EINVAL typically means:");
            LOG_ERROR_LOC("  1. Invalid serial port parameters in modbus_new_rtu()");
            LOG_ERROR_LOC("  2. Serial port already opened by another process");
            LOG_ERROR_LOC("  3. Unsupported baud rate or parity settings");
            LOG_ERROR_LOC("  4. libmodbus version incompatibility");
        }
        
        this->connected_ = false;
        return false;
    }
    this->connected_ = true;
    LOG_INFO_LOC(("ModbusClient 成功连接到: " + port_).c_str());
    return true;
}

// modbus断开连接方法
void ModbusClient::disconnect()
{
    if (ctx_) {
        modbus_free(ctx_);
        modbus_close(ctx_);
        this->connected_ = false;
    }
}

bool ModbusClient::read_coils(int addr, int count, uint8_t *dest)
{
    std::shared_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    int rc = modbus_read_bits(ctx_, addr, count, dest);
    if (rc == count) {
        return true;
    }
    return false;
}

bool ModbusClient::read_input_bits(int addr, int count, uint8_t *dest)
{
    std::shared_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    int rc = modbus_read_input_bits(ctx_, addr, count, dest);
    if (rc == count) {
        return true;
    }
    return false;
}

bool ModbusClient::read_holding_registers(int addr, int count, uint16_t *dest)
{
    std::shared_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    // auto start = std::chrono::high_resolution_clock::now();
    int rc = modbus_read_registers(ctx_, addr, count, dest);
    if (rc == count) {
        // auto end = std::chrono::high_resolution_clock::now();
        // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        // std::cout << "读取保持寄存器耗时: " << duration.count() << " 微秒" << "\n";
        return true;
    }
    return false;
}

bool ModbusClient::read_input_registers(int addr, int count, uint16_t *dest)
{
    std::shared_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    int rc = modbus_read_input_registers(ctx_, addr, count, dest);
    if (rc == count) {
        return true;
    }
    return false;
}

ModbusClient::~ModbusClient()
{
    if (ctx_) {
    modbus_close(ctx_);
    modbus_free(ctx_);
    ctx_ = nullptr;
    }
}

// 写单个线圈 (功能码 05)
bool ModbusClient::write_coil(int addr, bool value)
{
    std::unique_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    if (!connected_ || ctx_ == nullptr) {
        return false;
    }
    
    int rc = modbus_write_bit(ctx_, addr, value ? 1 : 0);
    if (rc == 1) {
        return true;
    } else {
        LOG_ERROR_LOC("Write coil failed at address " + std::to_string(addr) + 
                     ", error: " + std::string(modbus_strerror(errno)));
        return false;
    }
}

// 写多个线圈 (功能码 15)
bool ModbusClient::write_coils(int addr, int count, const uint8_t* values)
{
    std::unique_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    if (!connected_ || ctx_ == nullptr) {
        return false;
    }
    
    int rc = modbus_write_bits(ctx_, addr, count, values);
    if (rc == count) {
        return true;
    } else {
        LOG_ERROR_LOC("Write coils failed at address " + std::to_string(addr) + 
                     ", count: " + std::to_string(count) + 
                     ", error: " + std::string(modbus_strerror(errno)));
        return false;
    }
}


// 写单个寄存器 (功能码 06)
bool ModbusClient::write_register(int addr, uint16_t value)
{
    std::unique_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    if (!connected_ || ctx_ == nullptr) {
        return false;
    }
    
    int rc = modbus_write_register(ctx_, addr, value);
    if (rc == 1) {
        return true;
    } else {
        LOG_ERROR_LOC("Write register failed at address " + std::to_string(addr) + 
                     ", error: " + std::string(modbus_strerror(errno)));
        return false;
    }
}

// 写多个寄存器 (功能码 16)
bool ModbusClient::write_registers(int addr, int count, const uint16_t* values)
{
    std::unique_lock<std::shared_mutex> lock(modbus_rw_mutex_);
    if (!connected_ || ctx_ == nullptr) {
        return false;
    }
    
    int rc = modbus_write_registers(ctx_, addr, count, values);
    if (rc == count) {
        return true;
    } else {
        LOG_ERROR_LOC("Write registers failed at address " + std::to_string(addr) + 
                     ", count: " + std::to_string(count) + 
                     ", error: " + std::string(modbus_strerror(errno)));
        return false;
    }
}



