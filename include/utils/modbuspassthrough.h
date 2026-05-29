#ifndef MODBUSPASSTHROUGH_H
#define MODBUSPASSTHROUGH_H

#include "modbus/modbus.h"
#include <string>
#include <vector>
#include <map>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>

/**
 * @brief Modbus 数据透传类
 * 
 * 功能：
 * 1. 从读取串口（如 ttyS1）轮询多个不同 ID 的设备
 * 2. 将数据整合到单一 ID，通过转发串口（如 ttyS2）或 Modbus TCP 提供访问
 * 3. 支持双向透传：读取和写入请求都能原样转发到对应设备
 */
class ModbusPassthrough {
public:
    // 转发模式枚举
    enum class ForwardMode {
        RTU,  // 转发到 RTU 串口
        TCP   // 转发到 TCP
    };

    /**
     * @brief 构造函数
     * @param read_port 读取串口号（如 "/dev/ttyS1"）
     * @param read_baudrate 读取串口波特率
     * @param forward_mode 转发模式（RTU 或 TCP）
     * @param forward_port 转发端口（RTU模式下为串口号如"/dev/ttyS2"，TCP模式下为端口号如"502"）
     * @param forward_ip TCP模式下的IP地址（仅TCP模式有效）
     * @param slave_id 对外暴露的统一从站ID
     */
    ModbusPassthrough(const std::string& read_port, int read_baudrate,
                     ForwardMode forward_mode,
                     const std::string& forward_port,
                     const std::string& forward_ip = "",
                     int slave_id = 1);
    
    ~ModbusPassthrough();

    /**
     * @brief 添加需要透传的设备（寄存器映射）
     * @param device_id 设备 Modbus ID
     * @param start_addr 起始寄存器地址
     * @param count 寄存器数量
     * @param mapped_addr 映射到统一地址空间的起始地址
     * @return 是否添加成功
     */
    bool add_device_mapping(uint8_t device_id, uint16_t start_addr, 
                           uint16_t count, uint16_t mapped_addr);

    /**
     * @brief 添加线圈映射（功能码 01/05/0f）
     * @param device_id 设备 Modbus ID
     * @param start_addr 起始线圈地址
     * @param count 线圈数量
     * @param mapped_addr 映射到统一地址空间的起始地址
     * @return 是否添加成功
     */
    bool add_coil_mapping(uint8_t device_id, uint16_t start_addr, 
                         uint16_t count, uint16_t mapped_addr);

    /**
     * @brief 添加离散输入映射（功能码 02）
     * @param device_id 设备 Modbus ID
     * @param start_addr 起始离散输入地址
     * @param count 离散输入数量
     * @param mapped_addr 映射到统一地址空间的起始地址
     * @return 是否添加成功
     */
    bool add_discrete_input_mapping(uint8_t device_id, uint16_t start_addr, 
                                   uint16_t count, uint16_t mapped_addr);

    /**
     * @brief 启动透传服务
     * @return 是否启动成功
     */
    bool start();

    /**
     * @brief 停止透传服务
     */
    void stop();

    /**
     * @brief 检查服务是否运行
     */
    inline bool is_running() const { return running_; }

    /**
     * @brief 设置连接回调（仅TCP模式有效）
     */
    using ConnectionCallback = std::function<void(int client_fd)>;
    using DisconnectionCallback = std::function<void(int client_fd)>;
    void set_connection_callback(ConnectionCallback callback);
    void set_disconnection_callback(DisconnectionCallback callback);

private:
    // 寄存器映射信息
    struct RegisterMapping {
        uint8_t device_id;      // 原始设备ID
        uint16_t device_addr;   // 原始设备地址
        uint16_t mapped_addr;   // 映射后的地址
        uint16_t count;         // 寄存器数量
    };

    // 线圈映射信息
    struct CoilMapping {
        uint8_t device_id;      // 原始设备ID
        uint16_t device_addr;   // 原始设备地址
        uint16_t mapped_addr;   // 映射后的地址
        uint16_t count;         // 线圈数量
    };

    // 离散输入映射信息
    struct DiscreteInputMapping {
        uint8_t device_id;      // 原始设备ID
        uint16_t device_addr;   // 原始设备地址
        uint16_t mapped_addr;   // 映射后的地址
        uint16_t count;         // 离散输入数量
    };

    // 客户端结构（仅TCP模式使用）
    struct Client {
        modbus_t* ctx;
        std::thread thread;
        int fd;
    };

    // 读取线程函数 - 从读取串口轮询所有设备
    void read_loop();

    // 转发线程函数 - 处理来自转发端的请求
    void forward_loop();

    // 处理单个TCP客户端（仅TCP模式）
    void handle_client(int client_fd);

    // 处理读取请求（寄存器）
    int handle_read_request(modbus_t* ctx, uint8_t* req, int req_length);

    // 处理写入请求（寄存器）
    int handle_write_request(modbus_t* ctx, uint8_t* req, int req_length);

    // 处理线圈读取请求（功能码 01）
    int handle_read_coils_request(modbus_t* ctx, uint8_t* req, int req_length);

    // 处理离散输入读取请求（功能码 02）
    int handle_read_discrete_inputs_request(modbus_t* ctx, uint8_t* req, int req_length);

    // 处理写单个线圈请求（功能码 05）
    int handle_write_single_coil_request(modbus_t* ctx, uint8_t* req, int req_length);

    // 处理写多个线圈请求（功能码 0f）
    int handle_write_multiple_coils_request(modbus_t* ctx, uint8_t* req, int req_length);

    // 从指定设备读取寄存器
    bool read_from_device(uint8_t device_id, uint16_t addr, uint16_t count, uint16_t* data);

    // 向指定设备写入寄存器
    bool write_to_device(uint8_t device_id, uint16_t addr, uint16_t count, const uint16_t* data);

    // 从指定设备读取线圈
    bool read_coils_from_device(uint8_t device_id, uint16_t addr, uint16_t count, uint8_t* data);

    // 从指定设备读取离散输入
    bool read_discrete_inputs_from_device(uint8_t device_id, uint16_t addr, uint16_t count, uint8_t* data);

    // 向指定设备写入单个线圈
    bool write_single_coil_to_device(uint8_t device_id, uint16_t addr, bool value);

    // 向指定设备写入多个线圈
    bool write_multiple_coils_to_device(uint8_t device_id, uint16_t addr, uint16_t count, const uint8_t* data);

    // 更新内部缓存数据（寄存器）
    void update_cache(uint16_t mapped_addr, uint16_t count, const uint16_t* data);

    // 获取缓存数据（寄存器）
    bool get_cached_data(uint16_t mapped_addr, uint16_t count, uint16_t* data);

    // 更新线圈缓存
    void update_coil_cache(uint16_t mapped_addr, uint16_t count, const uint8_t* data);

    // 获取线圈缓存
    bool get_cached_coils(uint16_t mapped_addr, uint16_t count, uint8_t* data);

    // 更新离散输入缓存
    void update_discrete_input_cache(uint16_t mapped_addr, uint16_t count, const uint8_t* data);

    // 获取离散输入缓存
    bool get_cached_discrete_inputs(uint16_t mapped_addr, uint16_t count, uint8_t* data);

    // 成员变量
    modbus_t* read_ctx_ = nullptr;      // 读取串口上下文
    modbus_t* forward_ctx_ = nullptr;   // 转发上下文（RTU或TCP监听）
    
    ForwardMode forward_mode_;
    std::string read_port_;
    int read_baudrate_;
    std::string forward_port_;
    std::string forward_ip_;
    int slave_id_;
    
    int server_socket_ = -1;  // TCP监听socket
    
    std::vector<RegisterMapping> mappings_;  // 寄存器映射表
    std::vector<CoilMapping> coil_mappings_;  // 线圈映射表
    std::vector<DiscreteInputMapping> discrete_input_mappings_;  // 离散输入映射表
    
    mutable std::shared_mutex cache_mutex_;  // 缓存数据保护锁
    std::map<uint16_t, uint16_t> data_cache_; // 寄存器缓存数据：<映射地址, 值>
    std::map<uint16_t, uint8_t> coil_cache_;  // 线圈缓存数据：<映射地址, 值>
    std::map<uint16_t, uint8_t> discrete_input_cache_;  // 离散输入缓存数据：<映射地址, 值>

    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    
    std::thread read_thread_;
    std::thread forward_thread_;
    
    // TCP客户端管理
    std::list<Client> clients_;
    std::mutex clients_mutex_;
    std::atomic<int> active_clients_{0};
    int max_connections_ = 5;
    
    // 回调函数
    ConnectionCallback on_connect_;
    DisconnectionCallback on_disconnect_;
};

#endif // MODBUSPASSTHROUGH_H
