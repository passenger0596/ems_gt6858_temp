#include "canoperator.h"
#include "log.h"
#include <iostream>
#include <algorithm>

CanOperator::CanOperator(const std::string& interface_name,
                         sockcanpp::CanId default_sender_id)
    : interface_name_(interface_name),
      default_sender_id_(default_sender_id),
      is_connected_(false)
{
    // 初始化时并不立即连接，由 connect() 显式调用
}

CanOperator::~CanOperator()
{
    disconnect();
}

bool CanOperator::connect()
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (is_connected_) return true;

    try {
        // 创建 sockcanpp 驱动实例
        driver_ = std::make_unique<sockcanpp::CanDriver>(interface_name_, sockcanpp::CanDriver::CAN_SOCK_RAW, default_sender_id_);
        is_connected_ = true;
        LOG_INFO_LOC("Successfully connected to CAN interface: " + interface_name_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Failed to connect to CAN interface " + interface_name_ + ": " + e.what());
        driver_.reset();
        is_connected_ = false;
        return false;
    }
}

void CanOperator::disconnect()
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (driver_) {
        driver_.reset();
    }
    is_connected_ = false;
    LOG_INFO_LOC("Disconnected from CAN interface: " + interface_name_);
}

bool CanOperator::is_connected() const
{
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);
    return is_connected_ && driver_ != nullptr;
}

bool CanOperator::set_default_sender_id(sockcanpp::CanId can_id)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    default_sender_id_ = can_id;
    if (driver_) {
        driver_->setDefaultSenderId(can_id);
    }
    return true;
}

bool CanOperator::set_filters(const sockcanpp::filtermap_t& filters)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!driver_) return false;

    try {
        driver_->setCanFilters(filters);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Failed to set CAN filters: " + std::string(e.what()));
        return false;
    }
}

void CanOperator::clear_filters()
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!driver_) return;
    try {
        // 传入空映射通常表示清除硬件过滤，接收所有
        driver_->setCanFilters({});
    } catch (...) {}
}

bool CanOperator::send_frame(sockcanpp::CanId can_id,
                             const std::vector<uint8_t>& payload)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!driver_) return false;

    try {
        // 构造发送消息
        sockcanpp::CanMessage msg(can_id, std::string(payload.begin(), payload.end()));
        driver_->sendMessage(msg);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("CAN send_frame error: " + std::string(e.what()));
        return false;
    }
}

bool CanOperator::send_frame(const sockcanpp::CanMessage& message)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!driver_) return false;

    try {
        driver_->sendMessage(message);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("CAN send_frame error: " + std::string(e.what()));
        return false;
    }
}

bool CanOperator::read_frame(sockcanpp::CanMessage& message, int timeout_ms)
{
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!driver_) return false;

    try {
        // 阻塞读取
        if (driver_->waitForMessages(std::chrono::milliseconds(timeout_ms))) {
            message = driver_->readMessage();
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<sockcanpp::CanMessage> CanOperator::read_available(int timeout_ms, size_t max_frames)
{
    std::vector<sockcanpp::CanMessage> result;
    std::unique_lock<std::shared_mutex> lock(rw_mutex_);
    if (!driver_) return result;

    try {
        if (driver_->waitForMessages(std::chrono::milliseconds(timeout_ms))) {
            auto queued = driver_->readQueuedMessages();
            while (!queued.empty() && result.size() < max_frames) {
                result.push_back(queued.front());
                queued.pop();
            }
        }
    } catch (...) {}
    return result;
}

bool CanOperator::read_frames_by_ids(const std::vector<sockcanpp::CanId>& expected_ids,
                                     std::unordered_map<uint32_t, sockcanpp::CanMessage>& messages,
                                     int timeout_ms)
{
    if (expected_ids.empty()) return false;

    auto start_time = std::chrono::steady_clock::now();
    size_t found_count = 0;
    
    // 在指定超时时间内循环检索特定 ID
    while (true) {
        sockcanpp::CanMessage msg;
        // 内部已经带锁
        if (read_frame(msg, 5)) {
            for (const auto& id : expected_ids) {
                if (msg.getCanId() == static_cast<uint32_t>(id)) {
                    messages[static_cast<uint32_t>(id)] = msg;
                    found_count++;
                    break;
                }
            }
        }

        if (found_count >= expected_ids.size()) break;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() >= timeout_ms) {
            break;
        }
    }

    return found_count > 0;
}
