#include "dg_hgm6100.h"
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
#include <sstream>
#include "log.h"

DgHgm6100n::DgHgm6100n(const std::string& name, int com, int id)
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    // 初始化JSON数据结构
    Device::init_json_structure(name);
    // 加载配置，使用HGM6100N专用的配置文件路径
    init_config(Config::DG_HGM6100N_COMMUNICATION_FILEPATH);
    
    // 初始化寄存器段配置（根据ACHengdu协议XML）
    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc01_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments01_ = Device::generate_segments_from_addresses(all_addresses, 2000);

    all_addresses.clear();

    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments03_ = Device::generate_segments_from_addresses(all_addresses, 124);

    init_useful_indexes_from_map(this->fc01_nameToAddr_map, this->segments01_,this->parsed_registers_fc01_);
  
    init_useful_indexes_from_map(this->fc03_nameToAddr_map, this->segments03_,this->parsed_registers_fc03_);
 
    build_parsed_registers();
    
    // 预分配数据缓冲区（避免每次重新分配）
    this->data_buffer_vec01_.resize(this->segments01_.size());

    this->data_buffer_vec03_.resize(this->segments03_.size());


}


void DgHgm6100n::init_config(const std::string& config_file) {
    // 直接使用父类的默认实现，ACHengdu不需要特殊处理
    Device::init_config(config_file);

}

void DgHgm6100n::parse_rawdata(const std::vector<uint16_t>& data_list)
{
    parse_rawdata_generic(data_list);
    update_alarm_status();
}

void DgHgm6100n::read_data(ModbusClient& mb_client)
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
        LOG_ERROR_LOC("Modbus read error for ACMeter_3366 " + get_name() + ": " + std::string(e.what()));
        safe_set_qt_data(false);
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void DgHgm6100n::update_alarm_status() {
    if (!this->alarm_level1.is_object() || this->alarm_level1.empty())
        return;

    for (const auto& [key, value] : this->alarm_level1.items()) {
        bool status = getValue<bool>(key);
        this->handle_alarm(key, 1, status);
    }
}