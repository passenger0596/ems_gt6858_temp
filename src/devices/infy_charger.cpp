#include "devices/infy_charger.h"
#include "log.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <chrono>

// ======================== 辅助: 当前时间字符串 ========================
static std::string get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// ======================== 辅助: Big-endian 字节序转换 ========================
static uint32_t read_uint32_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8)  |
           static_cast<uint32_t>(data[3]);
}

static uint16_t read_uint16_be(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8) |
           static_cast<uint16_t>(data[1]);
}

static void write_uint32_be(std::vector<uint8_t>& buf, size_t offset, uint32_t val) {
    buf[offset + 0] = (val >> 24) & 0xFF;
    buf[offset + 1] = (val >> 16) & 0xFF;
    buf[offset + 2] = (val >> 8)  & 0xFF;
    buf[offset + 3] =  val        & 0xFF;
}


// ======================== 构造函数 ========================
InfyCharger::InfyCharger(const std::string& name, int can_channel, int id,
                         int chargers_num)
    : Device(name, can_channel, id)
    , chargers_num_(chargers_num)
{
    // 初始化基础 JSON 结构
    init_json_structure(name);

    // 初始化告警键有序列表（预分配，实际填充在 init_config() 中完成）
    alarm_keys_.resize(chargers_num_);

    // 构建消息处理器 map（响应 CAN ID → 解析函数）
    // 系统电压电流
    message_handlers_[ID_RESP_VOLTAGE_CURRENT_INT] =
        [this](const std::vector<uint8_t>& data) {
            this->parse_sys_voltage_current(data);
        };

    // 模块电压电流 (响应 ID 0x0289F000-0x0289F004)
    for (int i = 0; i < 5; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_VOLTAGE_CURRENT_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_voltage_current(i, data);
            };
    }

    // 模块温度状态 (响应 ID 0x0284F000-0x0284F004)
    for (int i = 0; i < 5; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_TEMPERATURE_STATUS_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_temperature_status(i, data);
            };
    }

    // 模块输入电压 (响应 ID 0x0286F000-0x0286F004)
    for (int i = 0; i < 5; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_INPUT_VOLTAGE_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_input_voltage(i, data);
            };
    }

    // 初始化发送计时器
    last_read_send_    = std::chrono::steady_clock::now();
    last_control_send_ = std::chrono::steady_clock::now();

    LOG_INFO_LOC(("InfyCharger 设备初始化完成: " + name_ +
                  ", 模块数: " + std::to_string(chargers_num_)));
}

// ======================== init_config ========================
void InfyCharger::init_config(const std::string& config_file) {
    LOG_INFO_LOC(("加载 InfyCharger 配置文件: " + config_file));

    // 调用基类 init_config：解析 function_code03 的 hRegister → data_dict_ / dev_data_keys_
    //                      解析 dido → alarm_map / alarm_level1/2/3
    Device::init_config(config_file);

    // 重新构建 alarm_keys_: 按模块号分组告警键
    for (int i = 0; i < chargers_num_; ++i) {
        std::string prefix = "模块" + std::to_string(i + 1);
        alarm_keys_[i].clear();
        for (const auto& [alm_name, level] : alarm_map) {
            if (alm_name.find(prefix) == 0) {
                alarm_keys_[i].push_back(alm_name);
            }
        }
    }

    // 用嵌套的 data_to_qt 结构覆盖基类 init_config 创建的扁平数组
    init_data_to_qt();

    LOG_INFO_LOC(("InfyCharger 配置加载完成: " + std::to_string(data_dict_.size()) +
                  " 个寄存器, " + std::to_string(alarm_map.size()) + " 个告警项"));
}

// ======================== init_data_to_qt ========================
void InfyCharger::init_data_to_qt() {
    // 与 Python charger.py 的 data_to_qt 结构完全一致
    json qt_data = json::array();

    // [0]: 系统数据
    json sys_data = json::object();
    sys_data["系统总电压(V)"] = 0;
    sys_data["系统总电流(A)"] = 0;
    qt_data.push_back(sys_data);

    // [1..N]: 各模块数据
    for (int i = 0; i < chargers_num_; ++i) {
        json mod_data = json::object();
        mod_data["电压"]     = 0;
        mod_data["电流"]     = 0;
        mod_data["环温"]     = 0;
        mod_data["状态表0"]  = json::array({0,0,0,0,0,0,0,0});
        mod_data["状态表1"]  = json::array({0,0,0,0,0,0,0,0});
        mod_data["状态表2"]  = json::array({0,0,0,0,0,0,0,0});
        mod_data["AB线电压"] = 0;
        mod_data["BC线电压"] = 0;
        mod_data["CA线电压"] = 0;
        qt_data.push_back(mod_data);
    }

    std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
    data_to_qt["data"] = qt_data;
}

// ======================== read_data (CAN 读取线程入口) ========================
void InfyCharger::read_data(CanOperator& can_operator) {
    if (!can_operator.is_connected()) {
        this->online_status = false;
        safe_set_qt_data(false);
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // ── 1. 持续发送控制 CAN 帧（与 Python send_periodic 行为一致）──
    // 控制帧必须一直发送，不能只在值变化时发送。CAN 设备接收端需要持续收到
    // 控制报文才能维持工作状态。值变化时更新帧数据，但帧始终周期性发送。
    send_control_frames(can_operator);

    // ── 2. 按间隔发送所有读取 CAN 帧 ──
    bool send_ok = true;
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_read_send_).count() >= READ_SEND_INTERVAL_MS) {
        send_ok = send_all_read_frames(can_operator);
        last_read_send_ = now;
    }

    // ── 3. 读取并解析可用响应帧（返回是否收到有效响应）──
    bool read_ok = read_and_dispatch_responses(can_operator);

    // ── 4. 连续失败计数 → 标记离线 ──
    if (!send_ok && !read_ok) {
        reconnect_counter++;
        if (reconnect_counter > 5) {
            this->online_status = false;
            safe_set_qt_data(false);
        }
    } else if (read_ok) {
        reconnect_counter = 0;
    }
}

// ======================== send_all_read_frames ========================
bool InfyCharger::send_all_read_frames(CanOperator& can_operator) {
    // 空数据帧 (8 字节全 0)，用于读取请求
    static const std::vector<uint8_t> empty_payload(8, 0x00);
    bool all_ok = true;

    // 1. 系统电压电流 (整型)
    all_ok &= can_operator.send_frame(ID_READ_VOLTAGE_CURRENT_INT, empty_payload);

    // 2-6. 模块电压电流 (整型) — 始终发送 5 个模块的请求
    for (int i = 0; i < 5; ++i) {
        all_ok &= can_operator.send_frame(ID_READ_MODULE_VOLTAGE_CURRENT_INT_BASE + i, empty_payload);
    }

    // 7-11. 模块温度状态
    for (int i = 0; i < 5; ++i) {
        all_ok &= can_operator.send_frame(ID_READ_MODULE_TEMPERATURE_STATUS_BASE + i, empty_payload);
    }

    // 12-16. 模块输入电压
    for (int i = 0; i < 5; ++i) {
        all_ok &= can_operator.send_frame(ID_READ_MODULE_INPUT_VOLTAGE_BASE + i, empty_payload);
    }

    // 17. 模块限制参数
    all_ok &= can_operator.send_frame(ID_READ_MODULE_LIMIT, empty_payload);

    return all_ok;
}

// ======================== read_and_dispatch_responses ========================
bool InfyCharger::read_and_dispatch_responses(CanOperator& can_operator) {
    // 使用 read_available 批量读取当前队列中的帧
    auto messages = can_operator.read_available(10, 64);

    if (messages.empty()) {
        return false;
    }

    bool got_valid_frame = false;

    for (const auto& msg : messages) {
        // 仅处理扩展帧（与 Python is_extended_id 对应）
        if (!msg.isExtendedFrameId()) {
            continue;
        }

        got_valid_frame = true;

        uint32_t arb_id = static_cast<uint32_t>(msg.getCanId());
        auto it = message_handlers_.find(arb_id);
        if (it != message_handlers_.end()) {
            try {
                // 从 CanMessage 提取原始字节
                const auto& raw_data = msg.getFrameData();
                std::vector<uint8_t> data(raw_data.begin(), raw_data.end());
                if (data.size() >= 6) {  // 最小有效帧长度
                    it->second(data);
                }
            } catch (const std::exception& e) {
                std::stringstream hex_ss;
                hex_ss << "0x" << std::hex << arb_id << std::dec;
                LOG_WARNING_LOC(("InfyCharger 解析 CAN 消息失败, ID=" +
                                 hex_ss.str() + ": " + e.what()));
            }
        }
        // 未注册的 ID 静默忽略（与 Python 行为一致）
    }

    // 更新在线状态
    if (got_valid_frame) {
        this->online_status = true;
        safe_set_qt_data(true);
    }

    return got_valid_frame;
}

// ======================== on_message_received (单帧分发) ========================
void InfyCharger::on_message_received(const sockcanpp::CanMessage& msg) {
    if (!msg.isExtendedFrameId()) {
        this->online_status = false;
        safe_set_qt_data(false);
        return;
    }

    uint32_t arb_id = static_cast<uint32_t>(msg.getCanId());
    auto it = message_handlers_.find(arb_id);
    if (it != message_handlers_.end()) {
        const auto& raw_data = msg.getFrameData();
        std::vector<uint8_t> data(raw_data.begin(), raw_data.end());
        if (data.size() >= 6) {
            this->online_status = true;
            safe_set_qt_data(true);
            try {
                it->second(data);
            } catch (const std::exception& e) {
                std::stringstream hex_ss;
                hex_ss << "0x" << std::hex << arb_id << std::dec;
                LOG_WARNING_LOC(("InfyCharger 解析 CAN 消息失败, ID=" +
                                 hex_ss.str() + ": " + e.what()));
            }
        }
    }
}

// ======================== parse_sys_voltage_current ========================
void InfyCharger::parse_sys_voltage_current(const std::vector<uint8_t>& data) {
    // 8 字节: [uint32_be 电压][uint32_be 电流], 单位: 原始值/1000.0
    if (data.size() < 8) return;

    sys_voltage_ = static_cast<double>(read_uint32_be(data.data()))     / 1000.0;
    sys_current_ = static_cast<double>(read_uint32_be(data.data() + 4)) / 1000.0;

    // 更新 data_dict_
    updateRegisterValue("系统总电压(V)", sys_voltage_);
    updateRegisterValue("系统总电流(A)", sys_current_);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() && data_to_qt["data"].size() > 0) {
            data_to_qt["data"][0]["系统总电压(V)"] = sys_voltage_;
            data_to_qt["data"][0]["系统总电流(A)"] = sys_current_;
        }
    }
}

// ======================== parse_module_voltage_current ========================
void InfyCharger::parse_module_voltage_current(int module_num,
                                                const std::vector<uint8_t>& data) {
    if (data.size() < 8) return;
    if (module_num >= chargers_num_) return;  // 忽略超出配置的模块

    double voltage = static_cast<double>(read_uint32_be(data.data()))     / 1000.0;
    double current = static_cast<double>(read_uint32_be(data.data() + 4)) / 1000.0;

    std::string prefix = "模块" + std::to_string(module_num);

    updateRegisterValue(prefix + "电压(V)", voltage);
    updateRegisterValue(prefix + "电流(A)", current);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            data_to_qt["data"][module_num + 1]["电压"] = voltage;
            data_to_qt["data"][module_num + 1]["电流"] = current;
        }
    }
}

// ======================== parse_module_temperature_status ========================
void InfyCharger::parse_module_temperature_status(int module_num,
                                                   const std::vector<uint8_t>& data) {
    if (data.size() < 8) return;
    if (module_num >= chargers_num_) return;

    // 字节布局: [4B 预留][1B 温度][1B 状态表0][1B 状态表1][1B 状态表2]
    int temp     = static_cast<int>(data[4]);
    int status0  = static_cast<int>(data[5]);
    int status1  = static_cast<int>(data[6]);
    int status2  = static_cast<int>(data[7]);

    std::string prefix = "模块" + std::to_string(module_num);

    updateRegisterValue(prefix + "环温(℃)",  static_cast<double>(temp));
    updateRegisterValue(prefix + "状态表0",   static_cast<double>(status0));
    updateRegisterValue(prefix + "状态表1",   static_cast<double>(status1));
    updateRegisterValue(prefix + "状态表2",   static_cast<double>(status2));

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            data_to_qt["data"][module_num + 1]["环温"]     = temp;
            data_to_qt["data"][module_num + 1]["状态表0"]  = Device::uint16_to_switches(static_cast<uint16_t>(status0));
            data_to_qt["data"][module_num + 1]["状态表1"]  = Device::uint16_to_switches(static_cast<uint16_t>(status1));
            data_to_qt["data"][module_num + 1]["状态表2"]  = Device::uint16_to_switches(static_cast<uint16_t>(status2));
        }
    }
}

// ======================== parse_module_input_voltage ========================
void InfyCharger::parse_module_input_voltage(int module_num,
                                              const std::vector<uint8_t>& data) {
    if (data.size() < 6) return;
    if (module_num >= chargers_num_) return;

    // 6 字节: [uint16_be AB][uint16_be BC][uint16_be CA], 单位: 原始值/10.0
    double ab = static_cast<double>(read_uint16_be(data.data()))     / 10.0;
    double bc = static_cast<double>(read_uint16_be(data.data() + 2)) / 10.0;
    double ca = static_cast<double>(read_uint16_be(data.data() + 4)) / 10.0;

    std::string prefix = "模块" + std::to_string(module_num);

    updateRegisterValue(prefix + "AB线电压(V)", ab);
    updateRegisterValue(prefix + "BC线电压(V)", bc);
    updateRegisterValue(prefix + "CA线电压(V)", ca);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            data_to_qt["data"][module_num + 1]["AB线电压"] = ab;
            data_to_qt["data"][module_num + 1]["BC线电压"] = bc;
            data_to_qt["data"][module_num + 1]["CA线电压"] = ca;
        }
    }
}

// ======================== multiWriteCmdToDevice (CAN 控制入口) ========================
void InfyCharger::multiWriteCmdToDevice(std::shared_ptr<CanOperator> can_operator) {
    if (!can_operator || !can_operator->is_connected()) {
        LOG_WARNING_LOC(("InfyCharger: CAN 未连接，跳过控制写入"));
        return;
    }
    // 委托给核心实现（立即发送一次控制帧，不等下次 read_data 周期）
    send_control_frames(*can_operator);
}

// ======================== setCanControlParam (FC03 写入路由) ========================
void InfyCharger::setCanControlParam(const std::string& key, double value) {
    if (key == "充电机开关机") {
        set_on_off(static_cast<int>(value));
    } else if (key == "充电机设置电压(V)") {
        set_sys_voltage(value);
    } else if (key == "充电机设置电流(A)") {
        set_sys_current(value);
    }
}

// ======================== sendCanControlFrames (CAN 控制帧发送) ========================
void InfyCharger::sendCanControlFrames(std::shared_ptr<CanOperator> can_operator) {
    multiWriteCmdToDevice(can_operator);
}

// ======================== send_control_frames (shared_ptr 重载) ========================
void InfyCharger::send_control_frames(std::shared_ptr<CanOperator> can_operator) {
    if (!can_operator || !can_operator->is_connected()) return;
    send_control_frames(*can_operator);
}

// ======================== send_control_frames (核心实现，CanOperator&) ========================
void InfyCharger::send_control_frames(CanOperator& can_operator) {
    // ── 持续发送控制帧（与 Python send_periodic 行为一致）──
    // 充电机 CAN 控制需要一直发送报文，不能只在值变化时发送。
    // 值变化时更新帧数据，但帧始终周期性发送。

    bool on_off_changed   = (last_on_off_  != charger_on_off_);
    bool voltage_changed  = (std::fabs(last_voltage_ - set_sys_voltage_) > 0.001);
    bool current_changed  = (std::fabs(last_current_ - set_sys_current_) > 0.001);

    // 1. 开关机控制帧 — 每次循环都发送
    {
        const auto& payload = (charger_on_off_ == 1) ? charger_on_data_ : charger_off_data_;
        if (can_operator.send_frame(ID_WRITE_SYS_ON_OFF, payload)) {
            if (on_off_changed) {
                last_on_off_ = charger_on_off_;
                LOG_INFO_LOC(("InfyCharger 开关机切换: " + std::to_string(charger_on_off_)));
            }
        } else {
            // LOG_ERROR_LOC("InfyCharger 发送开关机 CAN 帧失败");
        }
    }

    // 2. 电压电流设置帧 — 每次循环都发送
    {
        std::vector<uint8_t> payload(8, 0x00);
        uint32_t v_raw = static_cast<uint32_t>(set_sys_voltage_ * 1000.0);
        uint32_t c_raw = static_cast<uint32_t>(set_sys_current_ * 1000.0);
        write_uint32_be(payload, 0, v_raw);
        write_uint32_be(payload, 4, c_raw);

        if (can_operator.send_frame(ID_WRITE_SYS_VOLTAGE_CURRENT, payload)) {
            if (voltage_changed || current_changed) {
                last_voltage_ = set_sys_voltage_;
                last_current_ = set_sys_current_;
                LOG_INFO_LOC(("InfyCharger 更新电压电流: V=" +
                              std::to_string(set_sys_voltage_) + "V, I=" +
                              std::to_string(set_sys_current_) + "A"));
            }
        } else {
            // LOG_ERROR_LOC("InfyCharger 发送电压电流 CAN 帧失败");
        }
    }
}

// ======================== update_alarm_status ========================
void InfyCharger::update_alarm_status() {
    try {
        for (int i = 0; i < chargers_num_; ++i) {
            std::string prefix = "模块" + std::to_string(i);

            // 读取三个状态表的值
            uint16_t status0 = static_cast<uint16_t>(getValue<double>(prefix + "状态表0", 0.0));
            uint16_t status1 = static_cast<uint16_t>(getValue<double>(prefix + "状态表1", 0.0));
            uint16_t status2 = static_cast<uint16_t>(getValue<double>(prefix + "状态表2", 0.0));

            // 展开位状态（与 Python parse_alarm 完全一致）
            std::vector<bool> alarm_bits;
            {
                auto s0 = Device::uint16_to_switches(status0);
                // 去掉最后两位
                s0.pop_back();
                s0.pop_back();
                // 去掉状态表0的第2位 (索引1)
                s0.erase(s0.begin() + 1);
                alarm_bits.insert(alarm_bits.end(), s0.begin(), s0.end());

                auto s1 = Device::uint16_to_switches(status1);
                alarm_bits.insert(alarm_bits.end(), s1.begin(), s1.end());

                auto s2 = Device::uint16_to_switches(status2);
                alarm_bits.insert(alarm_bits.end(), s2.begin(), s2.end());
            }

            // 与 alarm_keys_[i] 一一对应
            const auto& keys = alarm_keys_[i];
            size_t key_count = std::min(keys.size(), alarm_bits.size());

            for (size_t k = 0; k < key_count; ++k) {
                bool alarm_val = alarm_bits[k];
                handle_alarm(keys[k], 1, alarm_val);

                // 更新 data_to_qt 中的告警状态
                {
                    std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
                    data_to_qt[keys[k]] = alarm_val;
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("InfyCharger 解析告警失败: " + std::string(e.what())));
    }
}