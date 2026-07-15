#include "frontendcontroller.h"
#include "qtcontroller.h"
#include "log.h"

#include <sw/redis++/redis++.h>

// 定义静态成员变量
std::shared_ptr<FrontendController> FrontendController::instance_ = nullptr;

FrontendController::~FrontendController() {
    stop();
    LOG_INFO_LOC("FrontendController destroyed");
}

std::shared_ptr<FrontendController> FrontendController::getInstance() {
    if (!instance_) {
        instance_.reset(new FrontendController());
    }
    return instance_;
}

void FrontendController::start(const std::string& channel) {
    if (keep_running_.load()) {
        LOG_WARNING_LOC("FrontendController 已在运行中");
        return;
    }

    channel_ = channel;
    keep_running_ = true;
    listener_thread_ = std::make_unique<std::thread>(&FrontendController::listenLoop, this);
    LOG_INFO_LOC(("FrontendController 已启动，订阅频道: " + channel_).c_str());
}

void FrontendController::stop() {
    if (!keep_running_.load()) {
        return;
    }

    keep_running_ = false;

    // 强制关闭 Redis 连接，使阻塞的 consume() 返回
    if (redis_subscriber_) {
        try {
            redis_subscriber_->unsubscribe();
        } catch (...) {}
        redis_subscriber_.reset();
    }
    if (redis_client_) {
        redis_client_.reset();
    }

    if (listener_thread_ && listener_thread_->joinable()) {
        listener_thread_->join();
        listener_thread_.reset();
    }

    LOG_INFO_LOC("FrontendController 已停止");
}

void FrontendController::listenLoop() {
    try {
        // 配置 Redis 连接超时
        sw::redis::ConnectionOptions opts;
        opts.host = "127.0.0.1";
        opts.port = 6379;
        opts.socket_timeout = std::chrono::milliseconds(1000); // 1秒超时

        redis_client_ = std::make_unique<sw::redis::Redis>(opts);
        redis_subscriber_ = std::make_unique<sw::redis::Subscriber>(
            redis_client_->subscriber());

        // 设置消息回调
        redis_subscriber_->on_message([this](const std::string& channel, const std::string& msg) {
            try {
                json data = json::parse(msg);
                parse_frontend_data(data);
            } catch (const json::parse_error& e) {
                LOG_ERROR_LOC(("[FrontendController] JSON 解析失败: " + std::string(e.what())).c_str());
            } catch (const std::exception& e) {
                LOG_ERROR_LOC(("[FrontendController] 处理消息异常: " + std::string(e.what())).c_str());
            }
        });

        // 订阅前端控制频道
        redis_subscriber_->subscribe(channel_);
        LOG_INFO_LOC(("[FrontendController] 已订阅 Redis 频道: " + channel_).c_str());

        // 循环消费消息
        while (keep_running_.load()) {
            try {
                redis_subscriber_->consume();
            } catch (const sw::redis::TimeoutError& e) {
                // 超时属于正常现象，继续循环检查停止标志
                continue;
            } catch (const sw::redis::Error& e) {
                if (!keep_running_.load()) {
                    break;
                }
                LOG_ERROR_LOC(("[FrontendController] Redis 连接错误: " + std::string(e.what())).c_str());
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("[FrontendController] 订阅线程异常: " + std::string(e.what())).c_str());
    }
}

void FrontendController::parse_frontend_data(const json& data) {
    // 验证必需字段
    std::string device = data.value("device", "");
    std::string command = data.value("command", "");
    std::string cmd_type = data.value("type", "single");

    if (device.empty() || command.empty()) {
        LOG_WARNING_LOC(("[FrontendController] 无效指令（缺少 device/command）: " + data.dump()).c_str());
        return;
    }

    LOG_INFO_LOC(("[FrontendController] 收到指令: device=" + device +
                  ", command=" + command +
                  ", type=" + cmd_type).c_str());

    try {
        // 获取 QtController 单例（共享命令字典）
        auto qt = QtController::getInstance();
        std::lock_guard<std::mutex> lock(qt->cmd_mutex_);
        auto& cmd_from_qt = qt->cmd_from_qt;

        if (cmd_type == "multi") {
            // ── multi 模式：value 为数组，批量设置子命令 ──
            if (data.contains("value") && data["value"].is_array()) {
                cmd_from_qt[device][command] = true;
                for (const auto& item : data["value"]) {
                    if (item.is_object()) {
                        for (const auto& [key, value] : item.items()) {
                            cmd_from_qt[device][key] = value;
                            LOG_DEBUG_LOC(("[FrontendController] multi -> " + device +
                                           "." + key + " = " + value.dump()).c_str());
                        }
                    }
                }
            }
            return;
        }

        // ── single 模式（默认）──
        json value = data.value("value", json(nullptr));
        cmd_from_qt[device][command] = value;

        LOG_INFO_LOC(("[FrontendController] single -> " + device +
                       "." + command + " = " + value.dump()).c_str());

        // 特殊处理：sys_setTimer 需要附带 timingModeSet 数据
        if (command == "sys_setTimer") {
            if (data.contains("timingModeSet") && !data["timingModeSet"].is_null()) {
                const auto& timing_data = data["timingModeSet"];
                if (timing_data.is_object()) {
                    cmd_from_qt[device]["timingModeSet"] = timing_data;
                    LOG_INFO_LOC("[FrontendController] sys_setTimer 已附带 timingModeSet 数据");
                } else {
                    LOG_WARNING_LOC("[FrontendController] sys_setTimer 的 timingModeSet 数据格式错误，期望对象");
                    cmd_from_qt[device][command] = false; // 回退
                }
            } else {
                LOG_WARNING_LOC("[FrontendController] sys_setTimer 缺少有效的 timingModeSet 数据，已拒绝");
                cmd_from_qt[device][command] = false; // 回退（与 Python 端行为一致）
            }
        }

        // 特殊处理：sys_setDemandResponse 需要附带 demandResponseModeSet 数据
        if (command == "sys_setDemandResponse") {
            if (data.contains("demandResponseModeSet") && !data["demandResponseModeSet"].is_null()) {
                const auto& dr_data = data["demandResponseModeSet"];
                if (dr_data.is_array() || dr_data.is_object()) {
                    cmd_from_qt[device]["demandResponseModeSet"] = dr_data;
                    LOG_INFO_LOC("[FrontendController] sys_setDemandResponse 已附带 demandResponseModeSet 数据");
                } else {
                    LOG_WARNING_LOC("[FrontendController] sys_setDemandResponse 的 demandResponseModeSet 数据格式错误，期望数组或对象");
                    cmd_from_qt[device][command] = false; // 回退
                }
            } else {
                LOG_WARNING_LOC("[FrontendController] sys_setDemandResponse 缺少有效的 demandResponseModeSet 数据，已拒绝");
                cmd_from_qt[device][command] = false; // 回退（与 Python 端行为一致）
            }
        }

        // 透传消息中的其他字段作为兄弟 key
        for (const auto& [k, v] : data.items()) {
            if (k != "device" && k != "command" && k != "value" && k != "type" &&
                k != "timingModeSet" && k != "demandResponseModeSet") {
                cmd_from_qt[device][k] = v;
            }
        }

    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("[FrontendController] parse_frontend_data 异常: " + std::string(e.what())).c_str());
    }
}
