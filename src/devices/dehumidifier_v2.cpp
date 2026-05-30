#include "dehumidifier_v2.h"
#include "utils.h"
#include "config.h"
#include <iostream>
#include "modbusclient.h"
#include <thread>
#include <fstream>
#include <pugixml.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include "log.h"

DehumidifierV2::DehumidifierV2(const std::string& name, int com, int id) 
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    
    Device::init_json_structure(name);
    init_config(Config::DEHUMIDIFIER_V2_COMMUNICATION_FILEPATH);
    
    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes();

    this->data_buffer_vec_.resize(segments_.size());
}

void DehumidifierV2::init_config(const std::string& config_file) {
    Device::init_config(config_file);
    
    LOG_INFO_LOC("DehumidifierV2 Config loaded successfully: " + 
                 std::to_string(this->data_dict_.size()) + " registers.");
}

void DehumidifierV2::parse_rawdata(const std::vector<uint16_t>& data_list) 
{
    int index = 0;
    json data_array = json::array();
    
    // ✅ 步骤1: 使用临时变量，无锁处理数据
    for (const uint16_t& buffer_index : this->useful_indexes) {
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
        
        // 计算实际值
        double actual_value = 0.0;
        if (mag > 1.0) {
            actual_value = data_list[buffer_index] / mag + offset;
        } else {
            actual_value = static_cast<uint16_t>(data_list[buffer_index] + offset);
        }
        
        // ✅ 线程安全地更新寄存器值
        this->updateRegisterValue(key, actual_value);
        
        data_array[index] = actual_value;
        index++;
    }

    // ✅ 步骤2: 仅在最后原子替换时持锁
    {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["online_status"] = true;
        this->data_to_qt["data"] = data_array;
    }
}

void DehumidifierV2::read_data(ModbusClient& mb_client) 
{
    if (!mb_client.is_connected()){
        LOG_ERROR_LOC("ModbusClient is not connected.");
        return;
    }
    
    try {
        bool total_read_success = true;

        mb_client.set_slave(this->id_);
        
        for (size_t i = 0; i < this->segments_.size(); ++i) {
            const auto& segment = this->segments_[i];
            this->data_buffer_vec_[i].resize(segment.num_regs);
            bool read_success = mb_client.read_holding_registers(
                segment.start_addr, segment.num_regs, this->data_buffer_vec_[i].data());
            
            if (!read_success) {
                total_read_success = false;
                // LOG_ERROR_LOC("Modbus read failed for segment " + std::to_string(i + 1) + 
                //              " (start_addr: " + std::to_string(segment.start_addr) + 
                //              ", num_regs: " + std::to_string(segment.num_regs) + ")");
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
        }else{
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
        LOG_ERROR_LOC("Modbus read error for DehumidifierV2 " + get_name() + ": " + std::string(e.what()));
        {
            std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
            this->data_to_qt["online_status"] = false;
        }
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void DehumidifierV2::init_useful_indexes()
{
    uint16_t j=0;
    for (uint8_t i =0;i<this->segments_.size();++i){
        for (uint16_t index=this->segments_[i].start_addr; index<this->segments_[i].start_addr+this->segments_[i].num_regs; ++index){
            for (const auto& pair : this->fc03_nameToAddr_map) {
                if (pair.second == index) {
                    this->useful_indexes.push_back(j);
                    break;
                }
            }
            ++j;
        }
    }

    if (this->useful_indexes.size() != this->dev_data_keys_.size()) {
        LOG_WARNING_LOC("Warning: Useful indexes size (" + 
                       std::to_string(this->useful_indexes.size()) +
                       ") does not match data keys size (" + 
                       std::to_string(this->dev_data_keys_.size()) + ").");
    }
}