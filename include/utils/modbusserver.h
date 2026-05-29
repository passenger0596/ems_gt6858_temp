#ifndef MODBUSSERVER_H
#define MODBUSSERVER_H

#include "modbus/modbus.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <list>
#include <atomic>
#include <memory>

// 前向声明
class Device;


class ModbusServer {
public:
    // 回调函数类型定义
    using ConnectionCallback = std::function<void(int client_fd)>;
    using DisconnectionCallback = std::function<void(int client_fd)>;
    
    // RTU 从站构造函数
    ModbusServer(const std::string& port, int baudrate = 9600, int slave_id = 1);
    
    // TCP 从站构造函数
    ModbusServer(const std::string& ip, const std::string& port, int max_connections = 10);
    
    ~ModbusServer();

    // 启动服务器（阻塞监听）
    bool start();
    
    // 停止服务器
    void stop();
    
    // 检查服务器是否运行
    inline bool is_running() const { return running_; }
    
    // 设置从站ID（仅RTU模式）
    bool set_slave_id(int slave_id);
    
    // 获取当前上下文
    inline modbus_t* get_context() const { return ctx_; }

    // ==================== 数据区操作 ====================
    
    // 设置线圈状态（离散输出，功能码 01/05/15）
    bool set_coil(int addr, bool value);
    bool set_coils(int addr, int count, const uint8_t* values);
    
    // 设置离散输入状态（只读，功能码 02）
    bool set_input_bit(int addr, bool value);
    bool set_input_bits(int addr, int count, const uint8_t* values);
    
    // 设置保持寄存器（读写，功能码 03/06/16）
    bool set_holding_register(int addr, uint16_t value);
    bool set_holding_registers(int addr, int count, const uint16_t* values);
    
    // 设置输入寄存器（只读，功能码 04）
    bool set_input_register(int addr, uint16_t value);
    bool set_input_registers(int addr, int count, const uint16_t* values);
    
    // 获取数据区值
    bool get_coil(int addr, bool* value) const;
    bool get_input_bit(int addr, bool* value) const;
    bool get_holding_register(int addr, uint16_t* value) const;
    bool get_input_register(int addr, uint16_t* value) const;
    
    // 批量获取数据
    std::vector<uint8_t> get_coils(int addr, int count) const;
    std::vector<uint8_t> get_input_bits(int addr, int count) const;
    std::vector<uint16_t> get_holding_registers(int addr, int count) const;
    std::vector<uint16_t> get_input_registers(int addr, int count) const;
    
    // 初始化数据区大小
    void init_data_area(int num_coils = 100, 
                       int num_input_bits = 100,
                       int num_holding_regs = 100,
                       int num_input_regs = 100);

    // ==================== 设备数据映射 ====================
    
    /**
     * @brief 将设备数据字典映射到输入寄存器(功能码04)
     * @param device 设备指针
     * @param start_addr 起始地址(默认从0开始)
     * @return 成功映射的寄存器数量
     * 
     * 说明:
     * - 按照设备data_dict_的顺序连续映射
     * - INT16类型占1个寄存器,INT32类型占2个寄存器(大端序)
     * - 自动应用mag和offset进行值转换
     */
    int map_device_to_input_registers(std::shared_ptr<Device> device, uint16_t start_addr = 0);

    // ==================== 回调设置 ====================
    
    // 设置连接/断开回调
    void set_connection_callback(ConnectionCallback callback);
    void set_disconnection_callback(DisconnectionCallback callback);

private:
    // 新增：客户端结构
    struct Client {
        modbus_t* ctx;
        std::thread thread;
        int fd;
    };

    int server_socket_;  // 成员变量
    // 新增
    std::list<Client> clients_;
    std::mutex clients_mutex_;

    std::atomic<int> active_clients_{0};
    // 监听循环
    void listen_loop();
    
    // 处理单个客户端连接
    void handle_client(int client_fd);
    
    // 修改：handle_request 接收 ctx
    int handle_request(modbus_t* ctx, uint8_t* req, int req_length);
    
    modbus_t* ctx_ = nullptr;   // ✅ TCP: only for listen | RTU: main context
    modbus_mapping_t* mb_mapping_ = nullptr;  // ✅ 使用 libmodbus 的标准映射结构
    bool running_ = false;
    std::atomic<bool> should_stop_{false};
    
    // 服务器配置
    const std::string ip_;
    const std::string port_;
    const int baudrate_;
    const int max_connections_;
    const bool is_tcp_;
    
    // 线程安全
    mutable std::shared_mutex data_mutex_;
    
    // 回调函数
    ConnectionCallback on_connect_;
    DisconnectionCallback on_disconnect_;
    
    // 监听线程
    std::thread listen_thread_;
};

#endif // MODBUSSERVER_H
