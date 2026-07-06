#ifndef INCREASE_CHARGER_H
#define INCREASE_CHARGER_H

#include "devices/device.h"
#include "utils/canoperator.h"
#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

/**
 * @class IncreaseCharger
 * @brief 增量充电机设备类，通过 CAN 总线通信
 *
 * 完整移植自 Python increcharger.py，支持：
 * - 周期性 CAN 帧读取（各模块电压/电流/温度/状态/输入电压/电压设置/电流设置）
 * - CAN 控制帧发送（系统开关机、各模块电压电流设置）
 * - 多模块告警解析（基于状态表位展开）
 * - 断线自动重连（由 DeviceManager 管理 CAN 连接生命周期）
 *
 * CAN ID 位布局 (与 Python 注释一致):
 *   bit 28~26: 错误码，常为0，3bits
 *   bit 25~22: 设备号，4bits
 *   bit 21~16: 命令号，6bits
 *   bit 15~8:  目标地址，8bits
 *   bit 7~0:   源地址，8bits
 *
 * 发送 ID 以 0x13 开头，响应 ID 以 0x12 开头
 */
class IncreaseCharger : public Device {
public:
    /// 默认充电模块数量（与 Python config.py CHARGERS_NUM 一致）
    static constexpr int DEFAULT_CHARGERS_NUM = 3;

    /// CAN 读取帧发送间隔（毫秒）
    static constexpr int READ_SEND_INTERVAL_MS = 500;

    /// CAN 控制帧发送间隔（毫秒）
    static constexpr int CONTROL_SEND_INTERVAL_MS = 250;

    /**
     * @brief 构造函数
     * @param name 设备名称 (如 "chargers")
     * @param can_channel CAN 通道编号 (对应 Config::CAN_INTERFACES 中的 key)
     * @param id 设备 ID
     * @param chargers_num 充电模块数量 (默认 3)
     */
    IncreaseCharger(const std::string& name, int can_channel, int id,
                    int chargers_num = DEFAULT_CHARGERS_NUM);

    ~IncreaseCharger() override = default;

    // ======================== 重写 Device 虚函数 ========================

    /**
     * @brief Modbus 读取逻辑 — CAN 设备不使用，空实现
     */
    void read_data(ModbusClient&) override {
        // IncreaseCharger 是纯 CAN 设备，不支持 Modbus 通信
    }

    /**
     * @brief 加载配置文件（XML 告警 + 构建 alarm_keys_）
     */
    void init_config(const std::string& config_file) override;

    /**
     * @brief CAN 设备周期性读取逻辑
     * 由 DeviceManager::readCanDeviceThreadWithStopFlag 每 ~200ms 调用
     * - 每 500ms 发送一轮全部读取 CAN 帧
     * - 每 250ms 发送控制 CAN 帧
     * - 读取可用响应帧并分发给对应解析器
     */
    void read_data(CanOperator& can_operator) override;

    /**
     * @brief CAN 设备控制写入逻辑
     * 由 handleControlMessage 回调触发
     * - 系统开关机 (charger_on_off)
     * - 设置各模块电压电流 (set_sys_voltage / set_sys_current)
     */
    void multiWriteCmdToDevice(std::shared_ptr<CanOperator> can_operator) override;

    /**
     * @brief FC03 写入路由到充电机控制参数 setter
     * @param key   控制键名（"开关机控制"/"电压设定(V)"/"电流设定(A)"）
     * @param value 真实值
     */
    void setCanControlParam(const std::string& key, double value) override;

    /**
     * @brief 发送 CAN 控制帧（委托给 multiWriteCmdToDevice）
     */
    void sendCanControlFrames(std::shared_ptr<CanOperator> can_operator) override;

    /**
     * @brief 解析并更新各模块告警状态
     * 从状态表展开位 → 匹配告警键 → 调用 handle_alarm()
     */
    void update_alarm_status() override;

    // ======================== 控制接口 ========================

    /// 设置开关机状态 (0=关机, 1=开机)
    void set_on_off(int on_off) { charger_on_off_ = on_off; }

    /// 获取开关机状态
    int get_on_off() const { return charger_on_off_; }

    /// 设置系统电压设定值
    void set_sys_voltage(double voltage) { set_sys_voltage_ = voltage; }

    /// 获取系统电压设定值
    double get_sys_voltage() const { return set_sys_voltage_; }

    /// 设置系统电流设定值
    void set_sys_current(double current) { set_sys_current_ = current; }

    /// 便捷方法：同时设置电压和电流
    void set_voltage_current(double voltage, double current) {
        set_sys_voltage_ = voltage;
        set_sys_current_ = current;
    }

    /// 获取系统电流设定值
    double get_sys_current() const { return set_sys_current_; }

private:
    // ======================== 读取 CAN ID（由本设备发送，请求充电模块回传数据）========================

    /// 读模块0/1/2状态、电压、电流 (cmd=0x01)
    static constexpr uint32_t ID_READ_MODULE0_STATUS          = 0x1307C081;
    static constexpr uint32_t ID_READ_MODULE1_STATUS          = 0x1307C082;
    static constexpr uint32_t ID_READ_MODULE2_STATUS          = 0x1307C083;

    /// 读模块0/1/2电压设置值 (cmd=0x01)
    static constexpr uint32_t ID_READ_MODULE0_VOLTAGE_SETTING = 0x13010081;
    static constexpr uint32_t ID_READ_MODULE1_VOLTAGE_SETTING = 0x13010082;
    static constexpr uint32_t ID_READ_MODULE2_VOLTAGE_SETTING = 0x13010083;

    /// 读模块0/1/2电流设置值 (cmd=0x00)
    static constexpr uint32_t ID_READ_MODULE0_CURRENT_SETTING = 0x13010881;
    static constexpr uint32_t ID_READ_MODULE1_CURRENT_SETTING = 0x13010882;
    static constexpr uint32_t ID_READ_MODULE2_CURRENT_SETTING = 0x13010883;

    /// 读模块0/1/2温度 (cmd=0x00)
    static constexpr uint32_t ID_READ_MODULE0_TEMPERATURE     = 0x13008081;
    static constexpr uint32_t ID_READ_MODULE1_TEMPERATURE     = 0x13008082;
    static constexpr uint32_t ID_READ_MODULE2_TEMPERATURE     = 0x13008083;

    /// 读模块0/1/2输入电压 (cmd=0x31)
    static constexpr uint32_t ID_READ_MODULE0_INPUT           = 0x1307A081;
    static constexpr uint32_t ID_READ_MODULE1_INPUT           = 0x1307A082;
    static constexpr uint32_t ID_READ_MODULE2_INPUT           = 0x1307A083;

    // ======================== 写入/控制 CAN ID（由本设备发送，控制充电模块行为）========================

    /// 系统开关机控制
    static constexpr uint32_t ID_WRITE_SYS_ON_OFF              = 0x1307C080;
    /// 模块0/1/2电压电流设置
    static constexpr uint32_t ID_WRITE_MODULE0_VOLTAGE_CURRENT = 0x1307C081;
    static constexpr uint32_t ID_WRITE_MODULE1_VOLTAGE_CURRENT = 0x1307C082;
    static constexpr uint32_t ID_WRITE_MODULE2_VOLTAGE_CURRENT = 0x1307C083;

    // ======================== 响应 CAN ID（由充电模块回传，本设备接收并解析）========================

    /// 模块状态响应基地址 (0x1207C080, cmd=0x01)
    static constexpr uint32_t ID_RESP_MODULE_STATUS_BASE      = 0x1207C081;
    /// 模块温度响应基地址 (0x12008080, cmd=0x00)
    static constexpr uint32_t ID_RESP_MODULE_TEMPERATURE_BASE = 0x12008081;
    /// 模块输入电压响应基地址 (0x1207A080, cmd=0x31)
    static constexpr uint32_t ID_RESP_MODULE_INPUT_BASE       = 0x1207A081;
    /// 模块电压设置响应基地址 (0x12010080, cmd=0x01)
    static constexpr uint32_t ID_RESP_MODULE_VOLTAGE_SETTING_BASE = 0x12010081;
    /// 模块电流设置响应基地址 (0x12010880, cmd=0x00)
    static constexpr uint32_t ID_RESP_MODULE_CURRENT_SETTING_BASE = 0x12010881;

    // ======================== CAN 消息解析方法 ========================

    /**
     * @brief 接收 CAN 消息入口，根据 arbitration_id 分发到具体解析器
     */
    void on_message_received(const sockcanpp::CanMessage& msg);

    /// 解析模块状态响应 (8 字节: [cmd=0x01][电流3B big-endian][电压4B big-endian], /10.0)
    void parse_module_status(int module_num, const std::vector<uint8_t>& data);

    /// 解析模块温度响应 (8 字节: [cmd=0x00][4B预留][温度2B big-endian][2B预留], /10.0)
    void parse_module_temperature(int module_num, const std::vector<uint8_t>& data);

    /// 解析模块输入电压响应 (8 字节: [cmd=0x31][1B预留][AB 2B][BC 2B][CA 2B], /32.0)
    void parse_module_input_voltage(int module_num, const std::vector<uint8_t>& data);

    /// 解析模块电压设置响应 (8 字节: [cmd=0x01][5B预留][电压设置2B big-endian], /10.0)
    void parse_module_voltage_setting(int module_num, const std::vector<uint8_t>& data);

    /// 解析模块电流设置响应 (8 字节: [电流设置2B big-endian][6B预留], /10.0)
    void parse_module_current_setting(int module_num, const std::vector<uint8_t>& data);

    // ======================== 辅助方法 ========================

    /// 初始化 data_to_qt (Qt 前端 JSON 结构，与 Python 版本一致)
    void init_data_to_qt();

    /// 发送所有周期性读取 CAN 帧 (15 帧) — 返回 true 表示全部发送成功
    bool send_all_read_frames(CanOperator& can_operator);

    /// 发送控制 CAN 帧（开关机 + 各模块电压电流设定）— shared_ptr 重载
    void send_control_frames(std::shared_ptr<CanOperator> can_operator);

    /// 发送控制 CAN 帧 — 核心实现（CanOperator&，由 read_data 持续调用）
    void send_control_frames(CanOperator& can_operator);

    /// 批量读取并解析可用 CAN 响应帧 — 返回 true 表示收到至少一个有效响应
    bool read_and_dispatch_responses(CanOperator& can_operator);

    /// 从响应 ID 提取 0-based 模块号 (低 4 位 - 1)
    static int get_module_index(uint32_t arb_id) {
        return static_cast<int>(arb_id & 0x0F) - 1;
    }

    // ======================== 成员变量 ========================

    /// 充电模块数量
    int chargers_num_;

    // 实时数据
    double sys_voltage_ = 0.0;
    double sys_current_ = 0.0;

    // 控制状态（atomic: 策略线程写, CAN线程读）
    std::atomic<int> charger_on_off_{0};       ///< 开关机状态 (0=关机, 1=开机)
    std::atomic<double> set_sys_voltage_{0.0}; ///< 设定系统电压
    std::atomic<double> set_sys_current_{0.0}; ///< 设定系统电流

    // 控制缓存（仅 CAN 线程访问，无需 atomic）
    int last_on_off_ = -1;
    double last_voltage_ = -1.0;
    double last_current_ = -1.0;

    // 发送计时
    std::chrono::steady_clock::time_point last_read_send_;
    std::chrono::steady_clock::time_point last_control_send_;

    // CAN 控制帧数据缓冲
    /// 开机指令: [0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA]
    std::vector<uint8_t> charger_on_data_  = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAA};
    /// 关机指令: [0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55]
    std::vector<uint8_t> charger_off_data_ = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55};

    /// 消息处理器: CAN arbitration_id → 解析函数
    std::unordered_map<uint32_t, std::function<void(const std::vector<uint8_t>&)>> message_handlers_;

    /// 告警键有序列表: 模块索引(0-based) → [告警键名列表] (与 Python alarm_keys 结构一致)
    std::vector<std::vector<std::string>> alarm_keys_;
};

#endif // INCREASE_CHARGER_H
