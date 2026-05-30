#include "dcdc.h"
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
#include "sqlcpp.h"

Dcdc::Dcdc(const std::string& name, int com, int id) 
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    // 初始化JSON数据结构
    Device::init_json_structure(name);
    init_config(Config::EJDCDC_COMMUNICATION_FILEPATH);
    
    
    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes();

    
    this->alarm_bits.reserve(128); // 预留足够空间

    // 预分配数据缓冲区（避免每次重新分配）,每段读取的vector组成一个总的vector
    this->data_buffer_vec_.resize(segments_.size());

}

void Dcdc::init_config(const std::string& config_file) {
    // 首先调用父类的默认实现来加载基本寄存器配置
    Device::init_config(config_file);
    
    // 然后加载DCDC特有的告警配置
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(config_file.c_str());
    
    if (result) {
        pugi::xml_node root = doc.document_element();
        if (root) {
            // 使用父类提供的parse_alarm_config方法解析告警信息
            parse_alarm_config(root);
        }
    }
    
    LOG_INFO_LOC("DCDC Config loaded successfully: " + 
                 std::to_string(this->data_dict_.size()) + " registers, " + 
                 std::to_string(this->alarm_map.size()) + " alarms.");
}

void Dcdc::parse_rawdata(const std::vector<uint16_t>& data_list) 
{
    this->online_status = true;

    int index = 0;
    json data_array = json::array();
    
    // ✅ 步骤1: 使用临时变量，无锁处理数据
    for (const uint16_t& buffer_index : this->useful_indexes) {
        // 确保索引不越界
        if (index >= static_cast<int>(this->dev_data_keys_.size()) || buffer_index >= data_list.size()) {
            break;
        }
        
        const std::string& key = this->dev_data_keys_[index];
        
        // ✅ 线程安全地获取寄存器配置（mag, offset, datatype）
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
        auto val = data_list[buffer_index] / mag + offset;
        
        // 根据数据类型转换实际值
        if (mag > 1.0 && datatype.find("INT16") != std::string::npos) {
            actual_value = val;
        } else if (datatype.find("INT32") != std::string::npos) {
            if (buffer_index + 1 < data_list.size()) {
                actual_value = Utils::getUint32num(data_list[buffer_index], data_list[buffer_index+1], Utils::Endian::BIG) / mag + offset;
            }
        } else {
            actual_value = static_cast<uint16_t>(val);
        }
        
        // ✅ 线程安全地更新寄存器值
        this->updateRegisterValue(key, actual_value);
        
        // 填充临时JSON数组
        data_array.push_back(actual_value);
        
        index++;
    }
    
    // ✅ 步骤2: 仅在最后原子替换时持锁（极短时间）
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");

    {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["timestamp"] = ss.str();
        this->data_to_qt["online_status"] = true;
        this->data_to_qt["data"] = data_array;
    }  // 锁立即释放
    
    update_alarm_status();
}

void Dcdc::update_alarm_status() 
{
    this->alarm_bits.clear();
    {
        std::shared_lock<std::shared_mutex> lock(this->data_dict_rwlock_);
        for (const auto& fault_name : this->fault_words) {
            auto it = this->data_dict_.find(fault_name);
            if (it != this->data_dict_.end()) {
                uint16_t fault_word_value = static_cast<uint16_t>(it->second.value);
                std::vector<bool> word_bits = Utils::uint16_to_switches(fault_word_value);
                this->alarm_bits.insert(this->alarm_bits.end(), word_bits.begin(), word_bits.end());
            }
        }
    }

    // 获取当前时间字符串（使用通用工具函数）
    std::string now = Utils::getCurrentTimeString();
    
    // ✅ 使用基类的通用告警处理方法
    for (size_t i = 0; i < this->alarm_map.size(); ++i) {
        const std::string& alarm_name = this->alarm_map[i].first;
        const uint8_t& level = this->alarm_map[i].second;
        bool status = (i < alarm_bits.size()) ? alarm_bits[i] : false;
        
        // 调用通用方法处理告警
        this->handle_alarm(alarm_name, level, status, now);
    }
}


void Dcdc::read_data(ModbusClient& mb_client) 
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
                break;
            }
        }
           
        if (total_read_success){
            this->data_buffer.clear();
            // 合并所有段的数据，data_buffer_vec_：所有段
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
        LOG_ERROR_LOC("Modbus read error for PCS " + get_name() + ": " + std::string(e.what()));
        {
            std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
            this->data_to_qt["online_status"] = false;
        }
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void Dcdc::init_useful_indexes()
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

    // 确保索引数量与键数量一致（理想情况下）
    if (this->useful_indexes.size() != this->dev_data_keys_.size()) {
        LOG_WARNING_LOC("Warning: Useful indexes size (" + 
                       std::to_string(this->useful_indexes.size()) +
                       ") does not match data keys size (" + 
                       std::to_string(this->dev_data_keys_.size()) + ").");
    }
}
