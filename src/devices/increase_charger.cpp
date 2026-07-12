#include "devices/increase_charger.h"
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

/// 读取 3 字节 Big-endian 无符号整数 (data[0..2])
static uint32_t read_uint24_be(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 16) |
           (static_cast<uint32_t>(data[1]) << 8)  |
           static_cast<uint32_t>(data[2]);
}

/// 写入 4 字节 Big-endian uint32 到缓冲区
static void write_uint32_be(std::vector<uint8_t>& buf, size_t offset, uint32_t val) {
    buf[offset + 0] = (val >> 24) & 0xFF;
    buf[offset + 1] = (val >> 16) & 0xFF;
    buf[offset + 2] = (val >> 8)  & 0xFF;
    buf[offset + 3] =  val        & 0xFF;
}

/// 写入 3 字节 Big-endian uint24 到缓冲区
static void write_uint24_be(std::vector<uint8_t>& buf, size_t offset, uint32_t val) {
    buf[offset + 0] = (val >> 16) & 0xFF;
    buf[offset + 1] = (val >> 8)  & 0xFF;
    buf[offset + 2] =  val        & 0xFF;
}


// ======================== 构造函数 ========================
IncreaseCharger::IncreaseCharger(const std::string& name, int can_channel, int id,
                                 int chargers_num)
    : Device(name, can_channel, id)
    , chargers_num_(chargers_num)
{
    // 初始化基础 JSON 结构
    init_json_structure(name);

    // 初始化告警键有序列表（实际填充在 init_config() 中完成）
    alarm_keys_.resize(chargers_num_);

    // ── 构建消息处理器 map（响应 CAN ID → 解析函数）──
    // 模块状态 (响应 ID 0x1207C081-0x1207C083)
    for (int i = 0; i < chargers_num_; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_STATUS_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_status(i, data);
            };
    }

    // 模块温度 (响应 ID 0x12008081-0x12008083)
    for (int i = 0; i < chargers_num_; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_TEMPERATURE_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_temperature(i, data);
            };
    }

    // 模块输入电压 (响应 ID 0x1207A081-0x1207A083)
    for (int i = 0; i < chargers_num_; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_INPUT_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_input_voltage(i, data);
            };
    }

    // 模块电压设置 (响应 ID 0x12010081-0x12010083)
    for (int i = 0; i < chargers_num_; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_VOLTAGE_SETTING_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_voltage_setting(i, data);
            };
    }

    // 模块电流设置 (响应 ID 0x12010881-0x12010883)
    for (int i = 0; i < chargers_num_; ++i) {
        uint32_t resp_id = ID_RESP_MODULE_CURRENT_SETTING_BASE + i;
        message_handlers_[resp_id] =
            [this, i](const std::vector<uint8_t>& data) {
                this->parse_module_current_setting(i, data);
            };
    }

    // 初始化发送计时器
    last_read_send_    = std::chrono::steady_clock::now();
    last_control_send_ = std::chrono::steady_clock::now();

    // 预构建读取请求列表 (chargers_num_ 个模块 × 5 种读取类型)
    {
        const struct {
            uint32_t base_id;
            uint8_t cmd_flag;
        } read_types[] = {
            {ID_READ_MODULE_STATUS_BASE,          0x01},
            {ID_READ_MODULE_VOLTAGE_SETTING_BASE, 0x01},
            {ID_READ_MODULE_CURRENT_SETTING_BASE, 0x00},
            {ID_READ_MODULE_TEMPERATURE_BASE,     0x00},
            {ID_READ_MODULE_INPUT_BASE,           0x31},
        };
        read_requests_.reserve(chargers_num_ * 5);
        for (int i = 0; i < chargers_num_; ++i) {
            for (const auto& rt : read_types) {
                read_requests_.push_back({rt.base_id + static_cast<uint32_t>(i), rt.cmd_flag});
            }
        }
    }

    LOG_INFO_LOC(("IncreaseCharger 设备初始化完成: " + name_ +
                  ", 模块数: " + std::to_string(chargers_num_)));
}

// ======================== init_config ========================
void IncreaseCharger::init_config(const std::string& config_file) {
    LOG_INFO_LOC(("加载 IncreaseCharger 配置文件: " + config_file));

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

    LOG_INFO_LOC(("IncreaseCharger 配置加载完成: " + std::to_string(data_dict_.size()) +
                  " 个寄存器, " + std::to_string(alarm_map.size()) + " 个告警项"));
}

// ======================== init_data_to_qt ========================
void IncreaseCharger::init_data_to_qt() {
    // 与 Python increcharger.py 的 data_to_qt 结构完全一致
    //
    // data_to_qt["data"] = [
    //   {"系统总电压(V)": 0, "系统总电流(A)": 0},          // index 0: 系统数据
    //   {"模块1电压(V)": 0, "模块1电流(A)": 0, ...},       // index 1: 模块1
    //   {"模块2电压(V)": 0, "模块2电流(A)": 0, ...},       // index 2: 模块2
    //   {"模块3电压(V)": 0, "模块3电流(A)": 0, ...},       // index 3: 模块3
    // ]

    json qt_data = json::array();

    // [0]: 系统数据
    json sys_data = json::object();
    sys_data["系统总电压(V)"] = 0;
    sys_data["系统总电流(A)"] = 0;
    qt_data.push_back(sys_data);

    // [1..N]: 各模块数据 — 包含所有以 "模块{i}" 开头的 data_dict 键
    for (int i = 0; i < chargers_num_; ++i) {
        std::string prefix = "模块" + std::to_string(i + 1);
        json mod_data = json::object();

        // 遍历 data_dict_ 找出所有属于该模块的键
        for (const auto& [key, rd] : data_dict_) {
            if (key.find(prefix) == 0) {
                mod_data[key] = 0;
            }
        }
        qt_data.push_back(mod_data);
    }

    std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
    data_to_qt["data"] = qt_data;
}

// ======================== read_data (CAN 读取线程入口) ========================
void IncreaseCharger::read_data(CanOperator& can_operator) {
    if (!can_operator.is_connected()) {
        this->online_status = false;
        safe_set_qt_data(false);
        LOG_ERROR_LOC(("CAN 设备未连接: " + name_));
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // ── 1. 持续发送控制 CAN 帧（与 Python send_periodic 行为一致）──
    // 控制帧必须一直发送，不能只在值变化时发送。CAN 设备接收端需要持续收到
    // 控制报文才能维持工作状态。值变化时更新帧数据，但帧始终周期性发送。
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_control_send_).count() >= CONTROL_SEND_INTERVAL_MS) {
        send_control_frames(can_operator);
        last_control_send_ = now;
    }

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
bool IncreaseCharger::send_all_read_frames(CanOperator& can_operator) {
    bool all_ok = true;

    // 辅助: 构建带 cmd 字节的 8 字节 payload
    auto make_payload = [](uint8_t cmd) -> std::vector<uint8_t> {
        return {cmd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    };

    // 遍历构造函数预构建的 read_requests_, 每 500ms 调用只执行发送
    for (const auto& req : read_requests_) {
        if (!can_operator.send_frame(req.can_id, make_payload(req.cmd_flag))) {
            all_ok = false;
        }
    }

    return all_ok;
}

// ======================== read_and_dispatch_responses ========================
bool IncreaseCharger::read_and_dispatch_responses(CanOperator& can_operator) {
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

        uint32_t arb_id = static_cast<uint32_t>(msg.getCanId());

        // 检查 ID 是否在已注册的 message_handlers_ 中
        if (message_handlers_.find(arb_id) == message_handlers_.end()) {
            continue;
        }

        // 提取数据
        const auto& raw_data = msg.getFrameData();
        std::vector<uint8_t> data(raw_data.begin(), raw_data.end());

        // 忽略空数据（与 Python: if not msg.data or len(msg.data) < 1: return 一致）
        if (data.empty()) {
            continue;
        }

        // ── 与 Python on_message_received 完全一致的 cmd 过滤 ──
        // 模块状态响应 (0x1207C08x): data[0] 必须为 0x01
        if ((arb_id & 0xFFFFF0) == 0x1207C080 && data[0] != 0x01) {
            continue;
        }
        // 模块输入电压响应 (0x1207A08x): data[0] 必须为 0x31
        if ((arb_id & 0xFFFFF0) == 0x1207A080 && data[0] != 0x31) {
            continue;
        }
        // 模块温度响应 (0x1200808x): data[0] 必须为 0x00
        if ((arb_id & 0xFFFFF0) == 0x12008080 && data[0] != 0x00) {
            continue;
        }

        got_valid_frame = true;

        // 分发到对应的解析器
        auto it = message_handlers_.find(arb_id);
        if (it != message_handlers_.end()) {
            try {
                it->second(data);       // 调用对应的解析器去解析数据
                std::stringstream hex_ss;
                hex_ss << "0x" << std::hex << arb_id << std::dec;
                LOG_INFO_LOC(("IncreaseCharger 解析消息 ID=" + hex_ss.str()));
            } catch (const std::exception& e) {
                std::stringstream hex_ss;
                hex_ss << "0x" << std::hex << arb_id << std::dec;
                LOG_WARNING_LOC(("IncreaseCharger 解析 CAN 消息失败, ID=" +
                                 hex_ss.str() + ": " + e.what()));
            }
        }
    }

    // 更新在线状态和时间戳
    if (got_valid_frame) {
        this->online_status = true;
        safe_set_qt_data(true);
    }

    return got_valid_frame;
}

// ======================== on_message_received (单帧分发) ========================
void IncreaseCharger::on_message_received(const sockcanpp::CanMessage& msg) {
    if (!msg.isExtendedFrameId()) {
        this->online_status = false;
        safe_set_qt_data(false);
        return;
    }

    uint32_t arb_id = static_cast<uint32_t>(msg.getCanId());

    if (message_handlers_.find(arb_id) == message_handlers_.end()) {
        return;
    }

    const auto& raw_data = msg.getFrameData();
    std::vector<uint8_t> data(raw_data.begin(), raw_data.end());

    if (data.empty()) return;

    // cmd 过滤 (与 Python 一致)
    if ((arb_id & 0xFFFFF0) == 0x1207C080 && data[0] != 0x01) return;
    if ((arb_id & 0xFFFFF0) == 0x1207A080 && data[0] != 0x31) return;
    if ((arb_id & 0xFFFFF0) == 0x12008080 && data[0] != 0x00) return;

    auto it = message_handlers_.find(arb_id);
    if (it != message_handlers_.end()) {
        this->online_status = true;
        safe_set_qt_data(true);
        try {
            it->second(data);
        } catch (const std::exception& e) {
            std::stringstream hex_ss;
            hex_ss << "0x" << std::hex << arb_id << std::dec;
            LOG_WARNING_LOC(("IncreaseCharger 解析 CAN 消息失败, ID=" +
                             hex_ss.str() + ": " + e.what()));
        }
    }
}

// ======================== parse_module_status ========================
void IncreaseCharger::parse_module_status(int module_num,
                                           const std::vector<uint8_t>& data) {
    // 字节布局: [cmd=0x01][电流3B big-endian][电压4B big-endian]
    // 与 Python parse_module_status 完全一致
    if (data.size() < 8) return;
    if (module_num < 0 || module_num >= chargers_num_) return;

    // data[1:4] → 3 字节 big-endian, 原始值 / 10.0 = 电流(A)
    double current = static_cast<double>(read_uint24_be(data.data() + 1)) / 10.0;
    // data[4:8] → 4 字节 big-endian uint32, 原始值 / 10.0 = 电压(V)
    double voltage = static_cast<double>(read_uint32_be(data.data() + 4)) / 10.0;

    // data[6] = 状态表1, data[7] = 状态表0
    int status0 = static_cast<int>(data[7]);
    int status1 = static_cast<int>(data[6]);

    std::string prefix = "模块" + std::to_string(module_num + 1);

    // 更新 data_dict_
    updateRegisterValue(prefix + "电流(A)", current);
    updateRegisterValue(prefix + "电压(V)", voltage);
    updateRegisterValue(prefix + "状态表0", static_cast<double>(status0));
    updateRegisterValue(prefix + "状态表1", static_cast<double>(status1));

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            auto& mod = data_to_qt["data"][module_num + 1];
            mod[prefix + "电压(V)"] = voltage;
            mod[prefix + "电流(A)"] = current;
            mod[prefix + "状态表0"] = status0;
            mod[prefix + "状态表1"] = status1;
        }
    }
}

// ======================== parse_module_temperature ========================
void IncreaseCharger::parse_module_temperature(int module_num,
                                                const std::vector<uint8_t>& data) {
    // 字节布局: [cmd=0x00][4B预留][温度2B big-endian][2B预留]
    // 与 Python parse_module_temperature 完全一致
    if (data.size() < 6) return;
    if (module_num < 0 || module_num >= chargers_num_) return;

    // data[4:6] → 2 字节 big-endian uint16, 原始值 / 10.0 = 温度(℃)
    double temperature = static_cast<double>(read_uint16_be(data.data() + 4)) / 10.0;

    std::string prefix = "模块" + std::to_string(module_num + 1);

    updateRegisterValue(prefix + "环温(℃)", temperature);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            data_to_qt["data"][module_num + 1][prefix + "环温(℃)"] = temperature;
        }
    }
}

// ======================== parse_module_input_voltage ========================
void IncreaseCharger::parse_module_input_voltage(int module_num,
                                                  const std::vector<uint8_t>& data) {
    // 字节布局: [cmd=0x31][1B预留][AB 2B][BC 2B][CA 2B]
    // 与 Python parse_module_input_voltage 完全一致
    if (data.size() < 8) return;
    if (module_num < 0 || module_num >= chargers_num_) return;

    // 各线电压: 2 字节 big-endian uint16, 原始值 / 32.0 = 电压(V)
    double ab = static_cast<double>(read_uint16_be(data.data() + 2)) / 32.0;
    double bc = static_cast<double>(read_uint16_be(data.data() + 4)) / 32.0;
    double ca = static_cast<double>(read_uint16_be(data.data() + 6)) / 32.0;

    std::string prefix = "模块" + std::to_string(module_num + 1);

    updateRegisterValue(prefix + "AB线电压(V)", ab);
    updateRegisterValue(prefix + "BC线电压(V)", bc);
    updateRegisterValue(prefix + "CA线电压(V)", ca);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            auto& mod = data_to_qt["data"][module_num + 1];
            mod[prefix + "AB线电压(V)"] = ab;
            mod[prefix + "BC线电压(V)"] = bc;
            mod[prefix + "CA线电压(V)"] = ca;
        }
    }
}

// ======================== parse_module_voltage_setting ========================
void IncreaseCharger::parse_module_voltage_setting(int module_num,
                                                    const std::vector<uint8_t>& data) {
    // 字节布局: [cmd=0x01][5B预留][电压设置2B big-endian]
    // 与 Python parse_module_voltage_setting 完全一致
    if (data.size() < 8) return;
    if (module_num < 0 || module_num >= chargers_num_) return;

    // data[6:8] → 2 字节 big-endian uint16, 原始值 / 10.0 = 电压(V)
    double voltage_setting = static_cast<double>(read_uint16_be(data.data() + 6)) / 10.0;

    std::string prefix = "模块" + std::to_string(module_num + 1);

    updateRegisterValue(prefix + "电压设置(V)", voltage_setting);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            data_to_qt["data"][module_num + 1][prefix + "电压设置(V)"] = voltage_setting;
        }
    }
}

// ======================== parse_module_current_setting ========================
void IncreaseCharger::parse_module_current_setting(int module_num,
                                                    const std::vector<uint8_t>& data) {
    // 字节布局: [电流设置2B big-endian][6B预留]
    // 与 Python parse_module_current_setting 完全一致
    if (data.size() < 2) return;
    if (module_num < 0 || module_num >= chargers_num_) return;

    // data[0:2] → 2 字节 big-endian uint16, 原始值 / 10.0 = 电流(A)
    double current_setting = static_cast<double>(read_uint16_be(data.data())) / 10.0;

    std::string prefix = "模块" + std::to_string(module_num + 1);

    updateRegisterValue(prefix + "电流设置(A)", current_setting);

    // 更新 data_to_qt
    {
        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
        if (data_to_qt["data"].is_array() &&
            static_cast<size_t>(module_num + 1) < data_to_qt["data"].size()) {
            data_to_qt["data"][module_num + 1][prefix + "电流设置(A)"] = current_setting;
        }
    }
}

// ======================== multiWriteCmdToDevice (CAN 控制入口) ========================
void IncreaseCharger::multiWriteCmdToDevice(std::shared_ptr<CanOperator> can_operator) {
    if (!can_operator || !can_operator->is_connected()) {
        LOG_WARNING_LOC(("IncreaseCharger: CAN 未连接，跳过控制写入"));
        return;
    }
    // 委托给核心实现（立即发送一次控制帧，不等下次 read_data 周期）
    send_control_frames(*can_operator);
}

// ======================== setCanControlParam (FC03 写入路由) ========================
void IncreaseCharger::setCanControlParam(const std::string& key, double value) {
    if (key == "充电机开关机") {
        set_on_off(static_cast<int>(value));
    } else if (key == "充电机设置电压(V)") {
        set_sys_voltage(value);
    } else if (key == "充电机设置电流(A)") {
        set_sys_current(value);
    }
}

// ======================== sendCanControlFrames (CAN 控制帧发送) ========================
void IncreaseCharger::sendCanControlFrames(std::shared_ptr<CanOperator> can_operator) {
    multiWriteCmdToDevice(can_operator);
}

// ======================== send_control_frames (shared_ptr 重载) ========================
void IncreaseCharger::send_control_frames(std::shared_ptr<CanOperator> can_operator) {
    if (!can_operator || !can_operator->is_connected()) return;
    send_control_frames(*can_operator);
}

// ======================== send_control_frames (核心实现，CanOperator&) ========================
void IncreaseCharger::send_control_frames(CanOperator& can_operator) {
    // ── 持续发送控制帧（与 Python control_charger 行为一致）──
    // 充电机 CAN 控制需要一直发送报文。值变化时更新帧数据，但帧始终周期性发送。
    //
    // Python 发送 4 个周期性消息:
    //   1. 系统开关机 → ID_WRITE_SYS_ON_OFF
    //   2-4. 各模块电压电流 → ID_WRITE_MODULEx_VOLTAGE_CURRENT
    //
    // 电压电流帧格式: [0][per_module_current_mA 3B big-endian][per_module_voltage_mV 4B big-endian]

    bool on_off_changed   = (last_on_off_  != charger_on_off_);
    bool voltage_changed  = (std::fabs(last_voltage_ - set_sys_voltage_) > 0.001);
    bool current_changed  = (std::fabs(last_current_ - set_sys_current_) > 0.001);
    bool value_changed    = on_off_changed || voltage_changed || current_changed;

    // ── 1. 系统开关机控制帧 ──
    {
        const auto& payload = (charger_on_off_ == 1) ? charger_on_data_ : charger_off_data_;
        can_operator.send_frame(ID_WRITE_SYS_ON_OFF, payload);
    }

    // ── 2. 各模块电压电流设置帧 ──
    // Python: current_avg = int(self.set_sys_current * 1000 / 3)
    //         voltage_set = int(self.set_sys_voltage * 1000)
    //         byteArray = struct.pack('>B3s4s', 0, current_avg.to_bytes(3, 'big'), voltage_set.to_bytes(4, 'big'))
    {
        uint32_t per_module_current_ma = static_cast<uint32_t>(set_sys_current_ * 1000.0 / chargers_num_);
        uint32_t per_module_voltage_mv = static_cast<uint32_t>(set_sys_voltage_ * 1000.0);

        std::vector<uint8_t> payload(8, 0x00);
        payload[0] = 0x00;  // 首字节为 0 (与 Python struct.pack '>B' 对应)
        write_uint24_be(payload, 1, per_module_current_ma);  // Bytes 1-3: 电流 (mA)
        write_uint32_be(payload, 4, per_module_voltage_mv);  // Bytes 4-7: 电压 (mV)

        // 发送到每个模块
        for (int i = 0; i < chargers_num_; ++i) {
            can_operator.send_frame(ID_WRITE_MODULE_VOLTAGE_CURRENT_BASE + static_cast<uint32_t>(i), payload);
            if (i < chargers_num_ - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // ── 3. 记录变更日志 ──
        if (value_changed) {
            last_on_off_  = charger_on_off_;
            last_voltage_ = set_sys_voltage_;
            last_current_ = set_sys_current_;
            LOG_INFO_LOC(("IncreaseCharger 控制更新: on_off=" + std::to_string(charger_on_off_) +
                          ", V=" + std::to_string(set_sys_voltage_) + "V, I=" +
                          std::to_string(set_sys_current_) + "A, per_module_I=" +
                          std::to_string(per_module_current_ma) + "mA, per_module_V=" +
                          std::to_string(per_module_voltage_mv) + "mV"));
        }
    }
}

// ======================== update_alarm_status ========================
void IncreaseCharger::update_alarm_status() {
    // 与 Python increcharger.py 的 parse_alarm 完全一致
    try {
        // ── 1. 计算系统总电压 = max(各模块电压), 系统总电流 = sum(各模块电流) ──
        double max_voltage = 0.0;
        double total_current = 0.0;

        for (int i = 0; i < chargers_num_; ++i) {
            std::string prefix = "模块" + std::to_string(i + 1);
            double v = getValue<double>(prefix + "电压(V)", 0.0);
            double c = getValue<double>(prefix + "电流(A)", 0.0);
            if (v > max_voltage) max_voltage = v;
            total_current += c;
        }

        sys_voltage_ = max_voltage;
        sys_current_ = total_current;

        updateRegisterValue("系统总电压(V)", sys_voltage_);
        updateRegisterValue("系统总电流(A)", sys_current_);

        // 更新 data_to_qt 系统数据
        {
            std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
            if (data_to_qt["data"].is_array() && data_to_qt["data"].size() > 0) {
                data_to_qt["data"][0]["系统总电压(V)"] = sys_voltage_;
                data_to_qt["data"][0]["系统总电流(A)"] = sys_current_;
            }
        }

        // ── 2. 各模块告警位展开 ──
        for (int i = 0; i < chargers_num_; ++i) {
            std::string prefix = "模块" + std::to_string(i + 1);

            // 读取状态表值 (uint8 范围)
            uint8_t status0 = static_cast<uint8_t>(getValue<double>(prefix + "状态表0", 0.0));
            uint8_t status1 = static_cast<uint8_t>(getValue<double>(prefix + "状态表1", 0.0));

            // 展开为 8 位 bool 数组 (MSB first, 与 Python uint8_to_switches 一致)
            // 使用 Device::uint16_to_switches 并取低 8 位 (索引 8-15)
            auto s0_full = Device::uint16_to_switches(static_cast<uint16_t>(status0));
            auto s1_full = Device::uint16_to_switches(static_cast<uint16_t>(status1));

            // 提取低 8 位 (索引 8-15), MSB first
            std::vector<bool> list_1(s0_full.begin() + 8, s0_full.end());  // 状态表0 的 8 bits
            std::vector<bool> list_2(s1_full.begin() + 8, s1_full.end());  // 状态表1 的 8 bits

            // 合并为 total_alarm_list (16 bits)
            std::vector<bool> total_alarm_list;
            total_alarm_list.insert(total_alarm_list.end(), list_1.begin(), list_1.end());
            total_alarm_list.insert(total_alarm_list.end(), list_2.begin(), list_2.end());

            // 开机状态 = 状态表0 bit0 (MSB, list_1[0])
            bool power_on_status = list_1[0];
            updateRegisterValue(prefix + "开机状态", power_on_status ? 1.0 : 0.0);

            // 开机设置 = 状态表1 bit2 (MSB, list_2[2])
            bool power_on_setting = list_2[2];
            updateRegisterValue(prefix + "开机设置", power_on_setting ? 1.0 : 0.0);

            // 更新 data_to_qt
            {
                std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
                if (data_to_qt["data"].is_array() &&
                    static_cast<size_t>(i + 1) < data_to_qt["data"].size()) {
                    data_to_qt["data"][i + 1][prefix + "开机状态"] = power_on_status;
                    data_to_qt["data"][i + 1][prefix + "开机设置"] = power_on_setting;
                }
            }

            // ── 3. 告警处理: total_alarm_list[1:10] 与 alarm_keys_[i] 一一对应 ──
            // Python: for key,value in zip(self.alarm_keys[i], total_alarm_list[1:10]):
            const auto& keys = alarm_keys_[i];
            size_t alarm_start = 1;  // 跳过索引 0 (开机状态)
            size_t alarm_count = std::min(keys.size(), static_cast<size_t>(9));  // 最多 9 个告警位

            for (size_t k = 0; k < alarm_count; ++k) {
                size_t bit_index = alarm_start + k;
                if (bit_index < total_alarm_list.size()) {
                    bool alarm_val = total_alarm_list[bit_index];
                    handle_alarm(keys[k], 1, alarm_val);

                    // 更新 data_to_qt 中的告警状态 (Python: self.data_to_qt[key] = value)
                    {
                        std::unique_lock<std::shared_mutex> lock(data_to_qt_rwlock_);
                        data_to_qt[keys[k]] = alarm_val;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("IncreaseCharger 解析告警失败: " + std::string(e.what())));
    }
}