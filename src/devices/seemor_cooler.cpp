#include "seemor_cooler.h"
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

SeemorLiquidCooler::SeemorLiquidCooler(const std::string& name, int com, int id)
    : Device(name, com, id) {
    this->name_ = name;
    this->id_ = id;
    this->com_ = com; 
    Device::init_json_structure(name);
    init_config(Config::EJPCS_COMMUNICATION_FILEPATH);

    std::vector<uint16_t> all_addresses;
    for (const auto& pair : this->fc03_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments03_ = Device::generate_segments_from_addresses(all_addresses, 100);

    all_addresses.clear();

    for (const auto& pair : this->fc04_nameToAddr_map) {
        all_addresses.push_back(pair.second);
    }
    this->segments04_ = Device::generate_segments_from_addresses(all_addresses, 100);

    init_useful_indexes_from_map(this->fc03_nameToAddr_map, this->segments03_);
    init_useful_indexes_from_map(this->fc04_nameToAddr_map, this->segments04_);
    build_parsed_registers();

    this->alarm_bits.reserve(128);

    this->data_buffer_vec03_.resize(this->segments03_.size());
    this->data_buffer_vec04_.resize(this->segments04_.size());
}

void SeemorLiquidCooler::init_config(const std::string& config_file) {
    // 首先调用父类的默认实现来加载基本寄存器配置
    Device::init_config(config_file);

}

void SeemorLiquidCooler::parse_rawdata(const std::vector<uint16_t>& data_list)
{
    parse_rawdata_generic(data_list);
    update_alarm_status();
}

void SeemorLiquidCooler::update_alarm_status()
{
    uint16_t fault_word_value = getValue<uint16_t>("故障告警代码");
    auto it = this->fault_words.find(fault_word_value);
    if (it != this->fault_words.end()){
        this->handle_alarm(it->second, 1, true);
    }
}

void SeemorLiquidCooler::read_data(ModbusClient& mb_client)
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