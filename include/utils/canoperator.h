#ifndef CANOPERATOR_H
#define CANOPERATOR_H

#include "CanDriver.hpp"
#include "CanId.hpp"
#include "CanMessage.hpp"
#include <cstddef>
#include <cstdint>
#include <linux/can.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @class CanOperator
 * @brief 基于 libsockcanpp 封装的 CAN 通信操作类
 * 
 * 提供类似 ModbusClient 的接口风格，支持连接管理、帧收发、过滤器设置。
 * 内部使用库原生的 sockcanpp::CanId 和 sockcanpp::CanMessage，减少转换开销。
 * 内部使用读写锁 (shared_mutex) 保证多线程安全，适用于 DeviceManager 的多设备并发访问。
 */
class CanOperator {
public:
    /**
     * @brief 构造函数
     * @param interface_name CAN 接口名 (如 "can0", "can1")
     * @param default_sender_id 默认发送 ID
     */
    CanOperator(const std::string& interface_name,
                sockcanpp::CanId default_sender_id = 0);
    ~CanOperator();

    /**
     * @brief 连接到 CAN 接口
     * @return true 连接成功或已连接, false 失败
     */
    bool connect();

    /**
     * @brief 断开连接并释放资源
     */
    void disconnect();

    /**
     * @brief 检查当前是否已连接
     */
    bool is_connected() const;

    /**
     * @brief 获取接口名称
     */
    std::string get_interface_name() const { return interface_name_; }

    /**
     * @brief 设置默认发送者 ID
     */
    bool set_default_sender_id(sockcanpp::CanId can_id);

    /**
     * @brief 设置硬件过滤器映射
     * @param filters 过滤器映射 (ID -> Mask)
     */
    bool set_filters(const sockcanpp::filtermap_t& filters);

    /**
     * @brief 清除所有过滤器 (接收所有帧)
     */
    void clear_filters();

    /**
     * @brief 发送一个 CAN 帧
     * @param can_id 目标 ID
     * @param payload 数据 (0-8 字节)
     */
    bool send_frame(sockcanpp::CanId can_id,
                    const std::vector<uint8_t>& payload);

    /**
     * @brief 发送一个预定义的 CanMessage
     */
    bool send_frame(const sockcanpp::CanMessage& message);

    /**
     * @brief 阻塞读取单个 CAN 帧
     * @param message 输出参数，存储读到的消息
     * @param timeout_ms 等待超时时间 (毫秒)
     * @return true 读到有效帧, false 超时或出错
     */
    bool read_frame(sockcanpp::CanMessage& message, int timeout_ms = 20);

    /**
     * @brief 批量读取当前队列中所有可用的 CAN 帧
     * @param timeout_ms 初次等待超时
     * @param max_frames 最大读取数量
     * @return 读到的消息列表
     */
    std::vector<sockcanpp::CanMessage> read_available(int timeout_ms = 20,
                                                      size_t max_frames = 64);

    /**
     * @brief 精确匹配读取一组指定的 ID 帧
     * @param expected_ids 期望接收到的 ID 列表
     * @param messages 输出映射：ID -> 消息数据
     * @param timeout_ms 总等待时间
     * @return true 如果收到了至少一个期望的帧
     */
    bool read_frames_by_ids(const std::vector<sockcanpp::CanId>& expected_ids,
                            std::unordered_map<uint32_t, sockcanpp::CanMessage>& messages,
                            int timeout_ms = 50);

private:
    std::string interface_name_;
    sockcanpp::CanId default_sender_id_;
    bool is_connected_;
    
    std::unique_ptr<sockcanpp::CanDriver> driver_;
    mutable std::shared_mutex rw_mutex_;
};

#endif // CANOPERATOR_H
