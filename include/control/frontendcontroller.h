#ifndef FRONTEND_CONTROLLER_H
#define FRONTEND_CONTROLLER_H

#include "json.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// 前向声明 redis++ 类型
namespace sw {
namespace redis {
    class Redis;
    class Subscriber;
}
}

using json = nlohmann::json;

/**
 * @brief 前端控制指令监听器（对应 Python ems-victory 的 frontend_controller.py）
 *
 * 订阅 Redis frontend/control 频道，接收来自 Web 前端的控制指令，
 * 解析并写入 QtController::cmd_from_qt，由 strategy 的策略循环统一执行。
 *
 * 指令格式：
 * {
 *     "device": "ems",          // 设备名称
 *     "command": "sys_startup", // 命令名称
 *     "value": true,            // 命令值 (bool / int / float / str)
 *     "type": "single"          // 可选，默认 "single"；也支持 "multi"
 * }
 */
class FrontendController {
public:
    ~FrontendController();

    /// 单例获取实例
    static std::shared_ptr<FrontendController> getInstance();

    // 禁止拷贝和赋值
    FrontendController(const FrontendController&) = delete;
    FrontendController& operator=(const FrontendController&) = delete;

    /**
     * @brief 启动前端控制器（在独立线程中运行 Redis 订阅循环）
     * @param channel 要订阅的 Redis 频道，默认 "frontend/control"
     */
    void start(const std::string& channel = "frontend/control");

    /// 停止前端控制器
    void stop();

    /// 是否正在运行
    bool isRunning() const { return keep_running_.load(); }

    /**
     * @brief 解析前端 JSON 指令（与 QtController::parse_qt_data 格式一致）
     * @param data JSON 指令 {device, command, value, type}
     */
    void parse_frontend_data(const json& data);

private:
    FrontendController() = default;

    /// Redis 订阅循环（在独立线程中运行）
    void listenLoop();

    static std::shared_ptr<FrontendController> instance_;

    std::atomic<bool> keep_running_{false};
    std::unique_ptr<std::thread> listener_thread_;

    // Redis 连接
    std::unique_ptr<sw::redis::Redis> redis_client_;
    std::unique_ptr<sw::redis::Subscriber> redis_subscriber_;

    std::string channel_;
};

#endif // FRONTEND_CONTROLLER_H
