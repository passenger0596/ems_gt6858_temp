#include "wea1610.h"
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

Wea1610::Wea1610(const std::string& name, int com, int id) 
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    
    Device::init_json_structure(name);
    init_config(Config::WEA1610_COMMUNICATION_FILEPATH);
    
    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes();

    this->data_buffer_vec_.resize(segments_.size());
}

void Wea1610::init_config(const std::string& config_file) {
    Device::init_config(config_file);
    
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(config_file.c_str());
    
    if (result) {
        pugi::xml_node root = doc.document_element();
        if (root) {
            parse_alarm_config(root);
        }
    }
    
    LOG_INFO_LOC("Wea1610 Config loaded successfully: " + 
                 std::to_string(this->data_dict_.size()) + " registers, " + 
                 std::to_string(this->alarm_map.size()) + " alarms.");
}

void Wea1610::parse_rawdata(const std::vector<uint16_t>& data_list) 
{
    std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
    
    this->data_to_qt["online_status"] = true;

    int index = 0;
    for (const uint16_t& buffer_index : this->useful_indexes) {
        if (index >= static_cast<int>(this->dev_data_keys_.size()) || buffer_index >= data_list.size()) {
            break;
        }
        const std::string& key = this->dev_data_keys_[index];
        RegisterData& reg_data = this->data_dict_[key];
        
        if (reg_data.mag > 1.0) {
            reg_data.value = data_list[buffer_index] / reg_data.mag + reg_data.offset;
        } else {
            reg_data.value = static_cast<int16_t>(data_list[buffer_index]) + reg_data.offset;
        }
        
        this->data_to_qt["data"][index] = reg_data.value;
        index++;
    }
}

void Wea1610::read_data(ModbusClient& mb_client) 
{
    if (!mb_client.is_connected()){
        LOG_ERROR_LOC("ModbusClient is not connected.");
        return;
    }

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

            this->data_to_qt["online_status"] = true;
            this->online_status = true;
        }else{
            this->reconnect_counter++;
            if (this->reconnect_counter>3){
                this->data_to_qt["online_status"] = false;
                this->online_status = false;
                this->reconnect_counter = 0;
                LOG_ERROR_LOC("Modbus read failed: " + get_name());
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Modbus read error for Wea1610 " + get_name() + ": " + std::string(e.what()));
        this->data_to_qt["online_status"] = false;
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void Wea1610::init_useful_indexes()
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