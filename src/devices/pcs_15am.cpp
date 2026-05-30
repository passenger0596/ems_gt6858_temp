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
    this->segments_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes();

    this->alarm_bits.reserve(128); // 预留足够空间
    
    // 预分配数据缓冲区（避免每次重新分配）
    this->data_buffer_vec_.resize(this->segments_.size());
}

void Pcs_15am::init_config(const std::string& config_file) {
    // 首先调用父类的默认实现来加载基本寄存器配置
    Device::init_config(config_file);
    
    // 然后加载PCS特有的告警配置
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(config_file.c_str());
    
    if (result) {
        pugi::xml_node root = doc.document_element();
        if (root) {
            // 使用父类提供的parse_alarm_config方法解析告警信息
            parse_alarm_config(root);
        }
    }
    
    LOG_INFO_LOC(this->name_ + "初始化data_dict成功: " +
                 std::to_string(this->data_dict_.size()) + " registers, " +
                 std::to_string(this->alarm_map.size()) + " alarms.");
}

void Pcs_15am::parse_rawdata(const std::vector<uint16_t>& data_list)
{
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
        if (mag > 1.0 && datatype.find("INT16") != std::string::npos) {
            actual_value = static_cast<int16_t>(data_list[buffer_index]) / static_cast<double>(mag) + offset;
        } else if (datatype.find("INT32") != std::string::npos || 
                   datatype.find("UINT32") != std::string::npos) {
            if (buffer_index + 1 < data_list.size()) {
                actual_value = Utils::getUint32num(data_list[buffer_index], data_list[buffer_index+1], Utils::Endian::BIG) / mag + offset;
            }
        } else {
            actual_value = static_cast<double>(data_list[buffer_index]) / static_cast<double>(mag) + offset;
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
    }  // 锁立即释放
    
    // 更新告警状态
    this->update_alarm_status();
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

void Pcs_15am::read_data(ModbusClient& mb_client)
{
    // 实现Modbus读取逻辑
    // if (!mb_client) {
    //     this->data_to_qt["online_status"] = false;
    //     this->online_status = false;
    //     LOG_ERROR_LOC("ModbusClient is not initialized.");
    //     return;
    // }

    if (!mb_client.is_connected()){
        LOG_ERROR_LOC("ModbusClient is not connected.");
        return;
    }
    
    LOG_DEBUG_LOC("PCS " + get_name() + " 准备读取 " + std::to_string(this->segments_.size()) + " 个数据段");

    // 更新时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["timestamp"] = ss.str();
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

            this->reconnect_counter = 0;
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

void Pcs_15am::init_useful_indexes()
{
    LOG_DEBUG_LOC("PCS_15AM的fc03_nameToAddr_map大小: " + std::to_string(this->fc03_nameToAddr_map.size()));
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
        
        // 调试信息：显示缺失的寄存器
        LOG_DEBUG_LOC("调试信息 - 已找到索引的寄存器：");
        for (size_t idx : this->useful_indexes) {
            if (idx < this->dev_data_keys_.size()) {
                LOG_DEBUG_LOC("  " + this->dev_data_keys_[idx] + " (地址: " + 
                             std::to_string(this->fc03_nameToAddr_map[this->dev_data_keys_[idx]]) + ")");
            }
        }
    } else {
        LOG_INFO_LOC("PCS索引匹配成功：" + std::to_string(this->useful_indexes.size()) + 
                    " 个寄存器已索引");
    }
}