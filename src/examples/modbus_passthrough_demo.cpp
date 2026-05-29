#include "modbuspassthrough.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>

// 全局变量用于信号处理
std::shared_ptr<ModbusPassthrough> g_passthrough = nullptr;

void signal_handler(int sig)
{
    std::cout << "\n[INFO] Received signal " << sig << ", stopping passthrough...\n";
    if (g_passthrough) {
        g_passthrough->stop();
    }
}

int main()
{
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "=== Modbus RTU Passthrough Demo ===\n\n";
    
    // 配置参数
    std::string read_port = "/dev/ttyS1";      // 读取串口
    int read_baudrate = 9600;                   // 波特率
    
    // 选择转发模式
    bool use_tcp_forward = false;  // 设置为 true 使用 TCP 转发，false 使用 RTU 转发
    
    std::string forward_port;
    std::string forward_ip = "";
    ModbusPassthrough::ForwardMode forward_mode;
    
    if (use_tcp_forward) {
        forward_mode = ModbusPassthrough::ForwardMode::TCP;
        forward_port = "502";  // TCP 端口
        forward_ip = "0.0.0.0";  // 监听所有接口
        std::cout << "Forward Mode: TCP on port " << forward_port << "\n";
    } else {
        forward_mode = ModbusPassthrough::ForwardMode::RTU;
        forward_port = "/dev/ttyS2";  // 转发串口
        std::cout << "Forward Mode: RTU on " << forward_port << "\n";
    }
    
    int slave_id = 1;  // 对外暴露的统一从站ID
    
    std::cout << "\nConfiguration:\n";
    std::cout << "  Read Port: " << read_port << " @ " << read_baudrate << " baud\n";
    std::cout << "  Slave ID: " << slave_id << "\n\n";
    
    // 创建透传实例
    g_passthrough = std::make_shared<ModbusPassthrough>(
        read_port, read_baudrate,
        forward_mode, forward_port, forward_ip,
        slave_id
    );
    
    // 添加设备映射
    // 假设我们有3个设备：
    // - 设备ID 1: 寄存器 0-9 映射到统一地址 0-9
    // - 设备ID 2: 寄存器 0-9 映射到统一地址 10-19
    // - 设备ID 3: 寄存器 0-4 映射到统一地址 20-24
    
    std::cout << "Adding device mappings:\n";
    g_passthrough->add_device_mapping(1, 0, 10, 0);    // 设备1的10个寄存器
    g_passthrough->add_device_mapping(2, 0, 10, 10);   // 设备2的10个寄存器
    g_passthrough->add_device_mapping(3, 0, 5, 20);    // 设备3的5个寄存器
    
    // 添加线圈映射（功能码 01/05/0f）
    std::cout << "\nAdding coil mappings:\n";
    g_passthrough->add_coil_mapping(1, 0, 16, 100);    // 设备1的16个线圈 -> 映射地址100-115
    g_passthrough->add_coil_mapping(2, 0, 8, 116);     // 设备2的8个线圈 -> 映射地址116-123
    
    // 添加离散输入映射（功能码 02）
    std::cout << "\nAdding discrete input mappings:\n";
    g_passthrough->add_discrete_input_mapping(1, 0, 16, 200);  // 设备1的16个离散输入 -> 映射地址200-215
    g_passthrough->add_discrete_input_mapping(3, 0, 8, 216);   // 设备3的8个离散输入 -> 映射地址216-223
    
    std::cout << "\nMapping Summary:\n";
    std::cout << "  Registers:\n";
    std::cout << "    Device ID 1: registers [0-9]   -> Mapped addresses [0-9]\n";
    std::cout << "    Device ID 2: registers [0-9]   -> Mapped addresses [10-19]\n";
    std::cout << "    Device ID 3: registers [0-4]   -> Mapped addresses [20-24]\n";
    std::cout << "\n  Coils (FC 01/05/0f):\n";
    std::cout << "    Device ID 1: coils [0-15]      -> Mapped addresses [100-115]\n";
    std::cout << "    Device ID 2: coils [0-7]       -> Mapped addresses [116-123]\n";
    std::cout << "\n  Discrete Inputs (FC 02):\n";
    std::cout << "    Device ID 1: inputs [0-15]     -> Mapped addresses [200-215]\n";
    std::cout << "    Device ID 3: inputs [0-7]      -> Mapped addresses [216-223]\n\n";
    
    // 设置回调（可选）
    g_passthrough->set_connection_callback([](int client_fd) {
        std::cout << "[CALLBACK] New client connected: fd=" << client_fd << "\n";
    });
    
    g_passthrough->set_disconnection_callback([](int client_fd) {
        std::cout << "[CALLBACK] Client disconnected: fd=" << client_fd << "\n";
    });
    
    // 启动透传服务
    std::cout << "Starting passthrough service...\n";
    if (!g_passthrough->start()) {
        std::cerr << "[ERROR] Failed to start passthrough service\n";
        return -1;
    }
    
    std::cout << "\n[INFO] Passthrough service is running.\n";
    std::cout << "[INFO] Press Ctrl+C to stop.\n\n";
    
    // 主循环 - 保持程序运行
    while (g_passthrough->is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 可以在这里添加状态监控、数据统计等功能
        static int counter = 0;
        if (++counter % 10 == 0) {
            std::cout << "[STATUS] Service running for " << counter << " seconds...\n";
        }
    }
    
    std::cout << "\n[INFO] Passthrough service stopped.\n";
    
    return 0;
}
