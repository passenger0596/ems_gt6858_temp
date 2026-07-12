#include "pcs_15am.h"
#include "utils.h"
#include "config.h"
#include <iostream>
#include "modbusclient.h"
#include <fstream>
#include <pugixml.hpp>
#include <thread>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include "log.h"
#include "sqlcpp.h"


Pcs_15am::Pcs_15am(const std::string& name, int com, int id)
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    // 初始化JSON数据结构
    Device::init_json_structure(name);
    // 加载配置，使用PCS专用的配置文件路径（此处假定配置常量已定义，实际可能需要根据项目调整）
    init_config(Config::EJPCS_15AM_COMMUNICATION_FILEPATH);
    // 初始化有用索引映射
    
    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments03_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes_from_map(this->fc03_nameToAddr_map, this->segments03_);

    this->alarm_bits.reserve(128); // 预留足够空间
    
    // 预分配数据缓冲区（避免每次重新分配）
    this->data_buffer_vec03_.resize(this->segments03_.size());
}

void Pcs_15am::init_config(const std::string& config_file) {
    // 首先调用父类的默认实现来加载基本寄存器配置
    Device::init_config(config_file);
    
}

void Pcs_15am::parse_rawdata(const std::vector<uint16_t>& data_list)
{
    parse_rawdata_generic(data_list);
    update_alarm_status();
}

void Pcs_15am::update_alarm_status()
{
    // 根据PCS协议XML，告警位可能分布在多个故障字寄存器中
    // 例如：硬件故障字1(5888), 硬件故障字2(5889), 电网故障字(5890)等
    // 这里仿照dcdc逻辑，但使用PCS的寄存器名

    this->alarm_bits.clear();
    for (const auto& fault_name : this->fault_words) {
        auto it = this->data_dict_.find(fault_name);
        if (it != this->data_dict_.end()) {
            uint16_t fault_word_value = static_cast<uint16_t>(it->second.value);
            std::vector<bool> word_bits = Utils::uint16_to_switches(fault_word_value);
            this->alarm_bits.insert(this->alarm_bits.end(), word_bits.begin(), word_bits.end());
        }
    }

    // 获取当前时间字符串（使用通用工具函数）
    // ✅ 使用基类的通用告警处理方法
    for (size_t i = 0; i < this->alarm_map.size(); ++i) {
        const std::string& alarm_name = this->alarm_map[i].first;
        const uint8_t& level = this->alarm_map[i].second;
        bool status = (i < alarm_bits.size()) ? alarm_bits[i] : false;
        
        // 调用通用方法处理告警
        this->handle_alarm(alarm_name, level, status);
    }
}

void Pcs_15am::read_data(ModbusClient& mb_client)
{
    if (!mb_client.is_connected()){
        LOG_ERROR_LOC("ModbusClient is not connected.");
        return;
    }

    try {
        if (read_all_registers(mb_client)) {
            parse_rawdata(this->data_buffer);
            this->reconnect_counter = 0;
        } else {
            this->reconnect_counter++;
            if (this->reconnect_counter>3){
                safe_set_qt_data(false);
                this->online_status = false;
                this->reconnect_counter = 0;
                LOG_ERROR_LOC("Modbus read failed: " + get_name());
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Modbus read error for PCS " + get_name() + ": " + std::string(e.what()));
        safe_set_qt_data(false);
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}