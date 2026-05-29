#include <iostream>
#include <thread>
#include <chrono>
#include <signal.h>
#include <memory>
#include "modbuspassthrough.h"
#include "log.h"

// 全局变量
std::shared_ptr<ModbusPassthrough> g_passthrough = nullptr;
std::atomic<bool> g_running{true};

void signal_handler(int sig)
{
    LOG_INFO_F("Received signal %d, stopping...", sig);
    g_running = false;
    if (g_passthrough) {
        g_passthrough->stop();
    }
}

/**
 * @brief 初始化并启动 Modbus 透传服务
 * 
 * 这是一个独立的透传服务示例，可以集成到主程序中
 */
int main()
{
    // 注册信号处理器
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    LOG_INFO("=== Modbus RTU Passthrough Service ===");
    
    try {
        // ==================== 配置参数 ====================
        
        // 读取串口配置
        std::string read_port = "/dev/ttyS1";
        int read_baudrate = 9600;
        
        // 转发模式配置
        bool use_tcp = true;  // 设置为 false 使用 RTU 转发
        
        ModbusPassthrough::ForwardMode forward_mode;
        std::string forward_port;
        std::string forward_ip = "";
        
        if (use_tcp) {
            forward_mode = ModbusPassthrough::ForwardMode::TCP;
            forward_port = "1502";  // 使用非特权端口
            forward_ip = "0.0.0.0";
            LOG_INFO_F("Forward Mode: TCP on port %s", forward_port.c_str());
        } else {
            forward_mode = ModbusPassthrough::ForwardMode::RTU;
            forward_port = "/dev/ttyS2";
            LOG_INFO_F("Forward Mode: RTU on %s", forward_port.c_str());
        }
        
        int slave_id = 1;
        
        // ==================== 创建设备映射配置 ====================
        
        struct DeviceConfig {
            uint8_t device_id;
            uint16_t start_addr;
            uint16_t count;
            uint16_t mapped_addr;
            std::string description;
        };
        
        std::vector<DeviceConfig> device_configs = {
            {1, 0, 10, 0, "DCDC Converter #1 - Voltage/Current"},
            {2, 0, 10, 10, "DCDC Converter #2 - Voltage/Current"},
            {3, 0, 8, 20, "AC Meter - Power/Energy"},
            {4, 100, 5, 28, "Temperature Sensors"}
        };
        
        // ==================== 创建透传实例 ====================
        
        LOG_INFO("Creating passthrough instance...");
        g_passthrough = std::make_shared<ModbusPassthrough>(
            read_port,
            read_baudrate,
            forward_mode,
            forward_port,
            forward_ip,
            slave_id
        );
        
        // ==================== 添加设备映射 ====================
        
        LOG_INFO("Configuring device mappings:");
        for (const auto& config : device_configs) {
            bool success = g_passthrough->add_device_mapping(
                config.device_id,
                config.start_addr,
                config.count,
                config.mapped_addr
            );
            
            if (success) {
                LOG_INFO_F("  ✓ Device ID %d [%s]: addr[%d-%d] -> mapped[%d-%d]",
                    config.device_id,
                    config.description.c_str(),
                    config.start_addr,
                    config.start_addr + config.count - 1,
                    config.mapped_addr,
                    config.mapped_addr + config.count - 1
                );
            } else {
                LOG_ERROR_F("  ✗ Failed to add mapping for device ID %d", config.device_id);
            }
        }
        
        // ==================== 设置回调（可选）====================
        
        g_passthrough->set_connection_callback([](int client_fd) {
            LOG_INFO_F("Client connected: fd=%d", client_fd);
        });
        
        g_passthrough->set_disconnection_callback([](int client_fd) {
            LOG_INFO_F("Client disconnected: fd=%d", client_fd);
        });
        
        // ==================== 启动服务 ====================
        
        LOG_INFO("Starting passthrough service...");
        if (!g_passthrough->start()) {
            LOG_ERROR("Failed to start passthrough service");
            return -1;
        }
        
        LOG_INFO("Passthrough service started successfully!");
        LOG_INFO("Press Ctrl+C to stop.");
        
        // ==================== 主循环 ====================
        
        int counter = 0;
        while (g_running && g_passthrough->is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            counter++;
            
            // 每10秒输出一次状态
            if (counter % 10 == 0) {
                LOG_INFO_F("Service running for %d seconds", counter);
            }
        }
        
        // ==================== 清理 ====================
        
        LOG_INFO("Stopping service...");
        g_passthrough->stop();
        
        LOG_INFO("Service stopped. Goodbye!");
        
    } catch (const std::exception& e) {
        LOG_ERROR_F("Exception: %s", e.what());
        return -1;
    }
    
    return 0;
}
