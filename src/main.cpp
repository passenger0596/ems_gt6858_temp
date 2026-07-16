#include "device.h"
#include "devicemanager.h"
#include "strategy.h"
#include "unixsocketserver.h"
#include "frontendcontroller.h"
#include "config.h"
#include "log.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>

static std::atomic<bool> keep_running{true};
static std::atomic<int> signal_received{0};

// 改进的信号处理函数
void signal_handler(int signum) {
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        keep_running = false;
        signal_received = signum;
    } else if (signal_count >= 2) {
        exit(1);
    }
}

// 设置信号处理器
void setup_signal_handler() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}


// C++ 风格的时间格式化
std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto timeinfo = *std::localtime(&time_t_now);
    
    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%H:%M:%S");
    return oss.str();
}

int main() {
    // 确保日志位置信息启用
    LOG_INFO_LOC("程序启动，日志系统初始化完成");
    
    std::cout << "按 Ctrl+C 退出程序\n" ;
    std::cout << "（按一次Ctrl+C优雅停止，快速按两次强制退出）" << "\n";
    setup_signal_handler();

    try {
        std::shared_ptr<DeviceManager> device_manager = std::make_shared<DeviceManager>();  // 创建设备管理器实例
        device_manager->createReadThreads();        // 启动设备数据读取线程
        device_manager->startRunningLogThread();    // 启动运行日志线程
        device_manager->startModbusTcpServer();     // 启动 Modbus TCP 服务器
        device_manager->startDbInserterThread();     // 启动数据库定时插入线程

        // 启动云端控制订阅（与Python端保持一致）
        device_manager->startSubscribeCloudControl();

        // 启动前端控制器（订阅 Redis frontend/control 频道，对应 Python ems-victory 的 frontend_controller.py）
        FrontendController::getInstance()->start();
        
        // 设置控制消息回调（可选）
        // device_manager->setControlMessageCallback([](const std::string& channel, const std::string& message) {
        //     LOG_INFO_LOC(("自定义控制消息处理: channel=" + channel).c_str());
        //     // 这里可以添加自定义的控制逻辑
        // });
        
        // 启动Socket服务器（与qt通讯）
        std::unique_ptr<UnixSocketServer> socket_server = std::make_unique<UnixSocketServer>(Config::SOCKET_PATH, device_manager->devices_);
        socket_server->startServer();
        // 启动策略线程
        std::unique_ptr<Strategy> strategy = std::make_unique<Strategy>(device_manager);
        strategy->runningThread();


        
        // 主循环，检查是否收到停止信号
        while (keep_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            // 在停止标志设置后,不再执行数据发布和设备更新
            if (!keep_running.load()) {
                break;
            }
            
            device_manager->publishDataToRedis();
            std::dynamic_pointer_cast<EMS>(device_manager->getDeviceByName("ems"))->update_ems();
            
            // 可以在这里添加其他定期检查或状态汇报
            static int counter = 0;
            if (++counter % 100 == 0) { // 每1秒打印一次
                std::cout << "程序运行中..." << "\n";
            }
        }
        
        std::cout << "正在停止所有线程..." << "\n";
        
        // 先停止策略线程
        strategy->stopThread();

        // 停止前端控制器
        FrontendController::getInstance()->stop();

        // 停止Socket服务器
        socket_server->stopServer();

        // 停止 Modbus TCP 服务器
        device_manager->stopModbusTcpServer();

        // 最后停止设备管理器的所有线程(包括日志线程和云端订阅线程)
        device_manager->stopAllThreads();
        
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << "程序正常退出。" << "\n";
    return 0;
}

