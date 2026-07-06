#ifndef INFY_CHARGER_H
#define INFY_CHARGER_H

#include "devices/device.h"
#include "utils/canoperator.h"
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

/**
 * @class InfyCharger
 * @brief 英飞源充电机设备类，通过 CAN 总线通信
 *
 * 完整移植自 Python charger.py，支持：
 * - 周期性 CAN 帧读取（系统电压/电流、各模块电压/电流/温度/状态/输入电压）
 * - CAN 控制帧发送（系统开关机、电压电流设置）
 * - 多模块告警解析（基于状态表位展开）
 * - 断线自动重连（由 DeviceManager 管理 CAN 连接生命周期）
 */
class InfyCharger : public Device {
public:
    /// 默认充电模块数量（与 Python config.py CHARGERS_NUM 一致）
    static constexpr int DEFAULT_CHARGERS_NUM = 3;

    /// CAN 读取帧发送间隔（毫秒）
    static constexpr int READ_SEND_INTERVAL_MS = 500;

    /// CAN 控制帧发送间隔（毫秒）
    static constexpr int CONTROL_SEND_INTERVAL_MS = 100;

    /**
     * @brief 构造函数
     * @param name 设备名称 (如 "chargers")
     * @param can_channel CAN 通道编号 (对应 Config::CAN_INTERFACES 中的 key)
     * @param id 设备 ID
     * @param chargers_num 充电模块数量 (默认 3)
     */
    InfyCharger(const std::string& name, int can_channel, int id,
                int chargers_num = DEFAULT_CHARGERS_NUM);

    ~InfyCharger() override = default;

    // ======================== 重写 Device 虚函数 ========================

    /**
     * @brief Modbus 读取逻辑 — CAN 设备不使用，空实现
     */
    void read_data(ModbusClient&) override {
        // InfyCharger 是纯 CAN 设备，不支持 Modbus 通信
    }

    /**
     * @brief 加载配置文件（XML 告警 + 硬编码 data_dict）
     */
    void init_config(const std::string& config_file) override;

    /**
     * @brief CAN 设备周期性读取逻辑
     * 由 DeviceManager::readCanDeviceThreadWithStopFlag 每 ~200ms 调用
     * - 每 500ms 发送一轮全部读取 CAN 帧
     * - 读取可用响应帧并分发给对应解析器
     */
    void read_data(CanOperator& can_operator) override;

    /**
     * @brief CAN 设备控制写入逻辑
     * 由 handleControlMessage 回调触发
     * - 系统开关机 (charger_on_off)
     * - 设置电压电流 (set_sys_voltage / set_sys_current)
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
    // ======================== CAN ID 常量 ========================
    // 读取 CAN ID（由本设备发送，请求充电模块回传数据）

    /// 读系统总电压电流 (浮点型)
    static constexpr uint32_t ID_READ_VOLTAGE_CURRENT_FLOAT  = 0x02813FF0;
    /// 读系统模块数
    static constexpr uint32_t ID_READ_MODULES_NUMBER          = 0x02823FF0;
    /// 读单模块电压电流 基地址 (低 8 位为模块号)
    static constexpr uint32_t ID_READ_MODULE_VOLTAGE_CURRENT_BASE  = 0x028300F0;
    /// 读单模块温度状态 基地址
    static constexpr uint32_t ID_READ_MODULE_TEMPERATURE_STATUS_BASE = 0x028400F0;
    /// 读单模块输入电压 基地址
    static constexpr uint32_t ID_READ_MODULE_INPUT_VOLTAGE_BASE    = 0x028600F0;
    /// 读系统电压电流 (整型) — 周期性任务实际使用
    static constexpr uint32_t ID_READ_VOLTAGE_CURRENT_INT   = 0x02883FF0;
    /// 读单模块电压电流 (整型) 基地址 — 周期性任务实际使用
    static constexpr uint32_t ID_READ_MODULE_VOLTAGE_CURRENT_INT_BASE = 0x028900F0;
    /// 读模块限制参数
    static constexpr uint32_t ID_READ_MODULE_LIMIT           = 0x028A00F0;

    //  响应 CAN ID（由充电模块回传，本设备接收并解析）

    /// 系统电压电流响应 (整型)
    static constexpr uint32_t ID_RESP_VOLTAGE_CURRENT_INT    = 0x0288F03F;
    /// 模块电压电流响应基地址 (低 8 位为模块号)
    static constexpr uint32_t ID_RESP_MODULE_VOLTAGE_CURRENT_BASE = 0x0289F000;
    /// 模块温度状态响应基地址
    static constexpr uint32_t ID_RESP_MODULE_TEMPERATURE_STATUS_BASE = 0x0284F000;
    /// 模块输入电压响应基地址
    static constexpr uint32_t ID_RESP_MODULE_INPUT_VOLTAGE_BASE     = 0x0286F000;

    //  写入/控制 CAN ID（由本设备发送，控制充电模块行为）

    /// 系统开关机控制
    static constexpr uint32_t ID_WRITE_SYS_ON_OFF           = 0x029A3FF0;
    /// 系统电压电流设置
    static constexpr uint32_t ID_WRITE_SYS_VOLTAGE_CURRENT  = 0x029B3FF0;
    /// 单模块电压电流设置 基地址
    static constexpr uint32_t ID_WRITE_MODULE_VOLTAGE_CURRENT_BASE = 0x029C00F0;

    // ======================== CAN 消息解析方法 ========================

    /**
     * @brief 接收 CAN 消息入口，根据 arbitration_id 分发到具体解析器
     */
    void on_message_received(const sockcanpp::CanMessage& msg);

    /// 解析系统总电压电流响应 (8 字节: 2×uint32_be, /1000.0)
    void parse_sys_voltage_current(const std::vector<uint8_t>& data);

    /// 解析单模块电压电流响应 (8 字节: 2×uint32_be, /1000.0)
    void parse_module_voltage_current(int module_num, const std::vector<uint8_t>& data);

    /// 解析单模块温度/状态响应 (8 字节: [4B预留][1B温度][1B状态0][1B状态1][1B状态2])
    void parse_module_temperature_status(int module_num, const std::vector<uint8_t>& data);

    /// 解析单模块输入电压响应 (6 字节: 3×uint16_be, /10.0)
    void parse_module_input_voltage(int module_num, const std::vector<uint8_t>& data);

    // ======================== 辅助方法 ========================

    /// 初始化 data_to_qt (Qt 前端 JSON 结构，与 Python 版本一致)
    void init_data_to_qt();

    /// 发送所有周期性读取 CAN 帧 (17 帧) — 返回 true 表示全部发送成功
    bool send_all_read_frames(CanOperator& can_operator);

    /// 发送控制 CAN 帧（开关机 + 电压电流设定）— shared_ptr 重载
    void send_control_frames(std::shared_ptr<CanOperator> can_operator);

    /// 发送控制 CAN 帧 — 核心实现（CanOperator&，由 read_data 持续调用）
    void send_control_frames(CanOperator& can_operator);

    /// 批量读取并解析可用 CAN 响应帧 — 返回 true 表示收到至少一个有效响应
    bool read_and_dispatch_responses(CanOperator& can_operator);

    // ======================== 成员变量 ========================

    /// 充电模块数量
    int chargers_num_;

    // 实时数据
    double sys_voltage_ = 0.0;
    double sys_current_ = 0.0;

    // 控制状态
    int charger_on_off_ = 0;        ///< 开关机状态 (0=关机, 1=开机)
    double set_sys_voltage_ = 0.0;  ///< 设定系统电压
    double set_sys_current_ = 0.0;  ///< 设定系统电流

    // 控制缓存（避免重复发送相同的控制指令）
    int last_on_off_ = -1;
    double last_voltage_ = -1.0;
    double last_current_ = -1.0;

    // 发送计时
    std::chrono::steady_clock::time_point last_read_send_;
    std::chrono::steady_clock::time_point last_control_send_;

    // CAN 控制帧数据缓冲
    std::vector<uint8_t> charger_on_data_  = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::vector<uint8_t> charger_off_data_ = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    /// 消息处理器: CAN arbitration_id → 解析函数
    /// 使用 uint32_t 作为键（sockcanpp::CanId 可隐式转换）
    std::unordered_map<uint32_t, std::function<void(const std::vector<uint8_t>&)>> message_handlers_;

    /// 告警键有序列表: 模块号 → [告警键名列表] (与 Python alarm_keys 结构一致)
    std::vector<std::vector<std::string>> alarm_keys_;
};

#endif // INFY_CHARGER_H
