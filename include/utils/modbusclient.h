#ifndef MODBUSCLIENT_H
#define MODBUSCLIENT_H

#include "modbus/modbus.h"
#include <string>
#include <vector>
#include <mutex>
#include <shared_mutex>


class ModbusClient {
public:
    ModbusClient(const std::string& port,const int& baudrate=9600);
    ModbusClient(const std::string& ip, const std::string& port);
    ~ModbusClient();

    bool connect();
    void disconnect();
    inline bool is_connected() const { return connected_; }

    inline bool set_slave(int slave) {
        return modbus_set_slave(ctx_, slave) != -1;
    }


    // 读取线圈状态
    bool read_coils(int addr, int count, uint8_t* dest);

    // 读取输入状态
    bool read_input_bits(int addr, int count, uint8_t* dest);

    // 读取保持寄存器
    bool read_holding_registers(int addr, int count, uint16_t* dest);
    // 读取输入寄存器
    bool read_input_registers(int addr, int count, uint16_t* dest);

   // 写单个线圈 (功能码 05)
    bool write_coil(int addr, bool value);

    // 写单个寄存器 (功能码 06)
    bool write_register(int addr, uint16_t value);

    // 写多个线圈 (功能码 15)
    bool write_coils(int addr, int count, const uint8_t* values);

    // 写多个寄存器 (功能码 16)
    bool write_registers(int addr, int count, const uint16_t* values);

    // 重载版本，使用vector作为参数
    inline bool write_registers(int addr, const std::vector<uint16_t>& values) {
        return write_registers(addr, static_cast<int>(values.size()), values.data());
    }

private:
    modbus_t* ctx_ = nullptr;
    bool connected_ = false;
    const std::string ip_;
    const std::string port_;
    const int baudrate_;
    std::shared_mutex modbus_rw_mutex_;  // 读写锁，读操作可并发，写操作独占

};

#endif // MODBUSCLIENT_H