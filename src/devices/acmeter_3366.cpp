#include "acmeter_3366.h"
#include "utils.h"
#include "../include/settings/config.h"
#include <iostream>
#include "modbusclient.h"
#include <fstream>
#include <pugixml.hpp>
#include <thread>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include "log.h"

ACMeter_3366::ACMeter_3366(const std::string& name, int com, int id)
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    // 初始化JSON数据结构
    Device::init_json_structure(name);
    // 加载配置，使用ACMeter专用的配置文件路径
    init_config(Config::AC_METER_3366_COMMUNICATION_FILEPATH);
    
    // 初始化寄存器段配置（根据DTSD3366D协议XML）
    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes();
    
    // 预分配数据缓冲区（避免每次重新分配）
    this->data_buffer_vec_.resize(this->segments_.size());
}



void ACMeter_3366::init_config(const std::string& config_file) {
    // 直接使用父类的默认实现，ACMeter_3366不需要特殊处理
    Device::init_config(config_file);
    
    LOG_INFO_LOC("ACMeter_3366 Config loaded successfully: " + 
                 std::to_string(this->data_dict_.size()) + " registers.");
}

void ACMeter_3366::parse_rawdata(const std::vector<uint16_t>& data_list)
{
    this->online_status = true;

    int index = 0; // 对应dev_data_keys_和json data数组的索引
    json data_array = json::array();
    
    // ✅ 步骤1: 使用临时变量，无锁处理数据
    for (const uint16_t& buffer_index : useful_indexes) {
        // 确保索引不越界
        if (index >= static_cast<int>(this->dev_data_keys_.size()) || buffer_index >= data_list.size()) {
            break;
        }
        
        const std::string& key = this->dev_data_keys_[index];
        
        // ✅ 线程安全地获取寄存器配置
        double mag = 1.0;
        uint16_t offset = 0;
        std::string datatype;
        
        if (!this->getRegisterConfig(key, mag, offset, datatype)) {
            LOG_WARNING_LOC("未找到寄存器配置: " + key);
            index++;
            continue;
        }

        // 根据数据类型转换实际值
        double actual_value = 0.0;
        if (datatype.find("INT32") != std::string::npos || 
            datatype.find("UINT32") != std::string::npos) {
            // 32位数据需要两个寄存器
            if (buffer_index + 1 < data_list.size()) {
                actual_value = Utils::getUint32num(data_list[buffer_index], data_list[buffer_index + 1], Utils::Endian::BIG) / mag + offset;
            }
        } else if (datatype.find("INT16") != std::string::npos || 
                   datatype.find("UINT16") != std::string::npos) {
            // 16位数据
            actual_value = data_list[buffer_index] / mag + offset;
        } else {
            // 默认处理为16位数据
            actual_value = data_list[buffer_index] / mag + offset;
        }
        
        // ✅ 线程安全地更新寄存器值
        this->updateRegisterValue(key, actual_value);
        
        // 更新JSON数据数组
        data_array.push_back(actual_value);
        index++;
    }

    // ✅ 步骤2: 仅在最后原子替换时持锁
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");

    {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["timestamp"] = ss.str();
        this->data_to_qt["online_status"] = true;
        this->data_to_qt["data"] = data_array;
    }
}

void ACMeter_3366::read_data(ModbusClient& mb_client)
{
    // 实现Modbus读取逻辑
    if (!mb_client.is_connected()){
        LOG_ERROR_LOC("ModbusClient is not connected.");
        return;
    }
    
    try {
        bool total_read_success = true;

        mb_client.set_slave(this->id_);
        
        // 使用预分配的成员变量进行Modbus读取
        for (size_t i = 0; i < this->segments_.size(); ++i) {
            const auto& segment = this->segments_[i];
            this->data_buffer_vec_[i].resize(segment.num_regs);
            bool read_success = mb_client.read_holding_registers(
                segment.start_addr, segment.num_regs, this->data_buffer_vec_[i].data());
            
            if (!read_success) {
                total_read_success = false;
                // LOG_ERROR_LOC("Modbus read failed for segment " + std::to_string(i + 1) + 
                // " (start_addr: " + std::to_string(segment.start_addr) + 
                // ", num_regs: " + std::to_string(segment.num_regs) + ")");
                break;
            }
        }
           
        if (total_read_success){
            this->data_buffer.clear();
            for (const std::vector<uint16_t>& reg : this->data_buffer_vec_){
                this->data_buffer.insert(this->data_buffer.end(), 
                            std::begin(reg), std::end(reg));
            }
            parse_rawdata(this->data_buffer);


        } else {
            this->reconnect_counter++;
            if (this->reconnect_counter>3){
                {
                    std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
                    this->data_to_qt["online_status"] = false;
                }
                this->online_status = false;
                this->reconnect_counter = 0;
                LOG_ERROR_LOC("Modbus read failed: " + get_name());
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Modbus read error for ACMeter_3366 " + get_name() + ": " + std::string(e.what()));
        {
            std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
            this->data_to_qt["online_status"] = false;
        }
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void ACMeter_3366::init_useful_indexes()
{
    LOG_DEBUG_LOC("ACMeter_3366的fc03_nameToAddr_map大小: " + std::to_string(this->fc03_nameToAddr_map.size()));
    uint16_t j = 0;
    for (uint8_t i = 0; i < this->segments_.size(); ++i) {
        for (uint16_t index = this->segments_[i].start_addr; index < this->segments_[i].start_addr + this->segments_[i].num_regs; ++index) {
            for (const auto& pair : this->fc03_nameToAddr_map) {
                if (pair.second == index) {
                    this->useful_indexes.push_back(j);
                    break;
                }
            }
            ++j;
        }
    }

    // 确保索引数量与键数量一致（理想情况下）
    if (this->useful_indexes.size() != this->dev_data_keys_.size()) {
        LOG_WARNING_LOC("Warning: ACMeter_3366 useful indexes size (" + 
                       std::to_string(this->useful_indexes.size()) +
                       ") does not match data keys size (" + 
                       std::to_string(this->dev_data_keys_.size()) + ").");
        
        // 调试信息：显示缺失的寄存器
        LOG_DEBUG_LOC("ACMeter_3366调试信息 - 已找到索引的寄存器：");
        for (size_t idx : this->useful_indexes) {
            if (idx < this->dev_data_keys_.size()) {
                LOG_DEBUG_LOC("  " + this->dev_data_keys_[idx] + " (地址: " + 
                             std::to_string(this->fc03_nameToAddr_map[this->dev_data_keys_[idx]]) + ")");
            }
        }
    } else {
        LOG_INFO_LOC("ACMeter_3366索引匹配成功：" + std::to_string(this->useful_indexes.size()) + 
                    " 个寄存器已索引");
    }
}
