#include "bms_uhome.h"

#include "config.h"
#include "log.h"
#include "utils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

using ordered_json = nlohmann::ordered_json;

namespace {
std::string extract_unit_from_name(const std::string& key) {
    const std::size_t left = key.rfind('(');
    const std::size_t right = key.rfind(')');
    if (left == std::string::npos || right == std::string::npos || left >= right) {
        return "";
    }
    return key.substr(left + 1, right - left - 1);
}
}

BmsUhome::BmsUhome(const std::string& name, int com, int id)
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;

    Device::init_json_structure(name);
    init_config(Config::BMS_UHOME_COMMUNICATION_FILEPATH);
    init_default_can_mapping();
    this->register_cache_.assign(this->dev_data_keys_.size(), 0);
}

void BmsUhome::read_data(ModbusClient& mb_client)
{
    // BMS 使用 CAN 通讯，此处不实现 Modbus 读取
    (void)mb_client;
}

void BmsUhome::read_data(CanOperator& can_operator)
{
    // 使用库原生的 CanId 列表进行读取
    std::unordered_map<uint32_t, sockcanpp::CanMessage> rx_frames;
    bool success = can_operator.read_frames_by_ids(this->response_ids_, rx_frames, 200);

    if (success) {
        std::vector<uint16_t> raw_values;
        raw_values.reserve(this->response_ids_.size() * 4);

        // 按 ID 顺序拼装原始寄存器数组
        for (const auto& can_id : this->response_ids_) {
            uint32_t id = static_cast<uint32_t>(can_id);
            auto it = rx_frames.find(id);
            if (it != rx_frames.end()) {
                const auto& msg = it->second;
                const auto& frame = msg.getRawFrame();
                for (size_t i = 0; i + 1 < frame.can_dlc; i += 2) {
                    // 默认大端拼接 (MSB, LSB)
                    raw_values.push_back((frame.data[i] << 8) | frame.data[i+1]);
                }
            } else {
                // 缺失帧补零
                for (int j = 0; j < 4; ++j) raw_values.push_back(0);
            }
        }

        // 解析并存入 data_to_qt
        parse_rawdata(raw_values);
        this->online_status = true;
    } else {
        this->online_status = false;
        LOG_WARNING_LOC("BMS Uhome read timeout or no data");
    }
}

void BmsUhome::parse_rawdata(const std::vector<uint16_t>& data_list) {
    std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    this->data_to_qt["timestamp"] = ss.str();
    this->data_to_qt["online_status"] = true;

    for (size_t i = 0; i < this->dev_data_keys_.size() && i < data_list.size(); ++i) {
        const std::string& key = this->dev_data_keys_[i];
        RegisterData& reg_data = this->data_dict_[key];
        reg_data.value = convert_raw_value(data_list[i], reg_data);
        this->data_to_qt["data"][i] = reg_data.value;
    }
}

void BmsUhome::init_config(const std::string& config_file)
{
    // 加载协议映射文件 (JSON)
    std::ifstream f(config_file);
    if (!f.is_open()) {
        LOG_ERROR_LOC("打开BMS协议json文件失败: " + config_file);
        return;
    }

    try {
        // 使用有序 JSON 保证 data_dict_ 和 dev_data_keys_ 的顺序与文件一致
        nlohmann::ordered_json data = nlohmann::ordered_json::parse(f);
        
        for (auto it = data.begin(); it != data.end(); ++it) {
            const std::string& key = it.key();
            const auto& val = it.value();
            
            RegisterData reg;
            reg.value = val.value("value", 0.0);
            reg.datatype = val.value("datatype", "UINT16");
            reg.mag = val.value("mag", 1.0);
            reg.offset = val.value("offset", 0.0);
            
            this->data_dict_[key] = reg;
            this->dev_data_keys_.push_back(key);
        }
        
        LOG_INFO_LOC("BMS Uhome 协议加载成功: " + std::to_string(data_dict_.size()) + " items");
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("解析BMS协议json文件失败: " + std::string(e.what()));
    }
}

void BmsUhome::init_default_can_mapping() {
    // this->response_ids_.clear();
    // this->response_id_to_index_.clear();

    // this->response_base_id_ = 0x18000000U + (static_cast<uint32_t>(this->id_) << 8);
    // this->request_id_ = this->response_base_id_ + 0x80U;

    // const size_t frame_count =
    //     (this->dev_data_keys_.size() + this->registers_per_frame_ - 1) /
    //     this->registers_per_frame_;
    // for (size_t i = 0; i < frame_count; ++i) {
    //     const uint32_t can_id = this->response_base_id_ + static_cast<uint32_t>(i);
    //     this->response_ids_.push_back(can_id);
    //     this->response_id_to_index_[can_id] = i;
    // }
}

uint16_t BmsUhome::bytes_to_uint16(uint8_t high_byte, uint8_t low_byte) const {
    return static_cast<uint16_t>((static_cast<uint16_t>(high_byte) << 8) | low_byte);
}

double BmsUhome::convert_raw_value(uint16_t raw_value, const RegisterData& reg_data) const {
    const double mag = reg_data.mag == 1 ? 1.0 : static_cast<double>(reg_data.mag);
    if (reg_data.datatype.find("INT16") != std::string::npos) {
        const int16_t signed_value = Utils::unsigned_to_signed(raw_value);
        return static_cast<double>(signed_value) / mag + reg_data.offset;
    }
    return static_cast<double>(raw_value) / mag + reg_data.offset;
}

void BmsUhome::update_alarm_status() {
    // 基础实现，目前协议 JSON 中未定义告警位，后续可根据协议手册扩充
    // 参考 DCDC 实现，如果存在故障字寄存器，可以在此处解析 bit 位
}
