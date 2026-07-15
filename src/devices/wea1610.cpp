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
    this->segments03_ = Device::generate_segments_from_addresses(all_addresses, 60);

    init_useful_indexes_from_map(this->fc03_nameToAddr_map, this->segments03_);

    this->data_buffer_vec03_.resize(this->segments03_.size());
}

void Wea1610::init_config(const std::string& config_file) {
    Device::init_config(config_file);
}

void Wea1610::parse_rawdata(const std::vector<uint16_t>& data_list) 
{
    parse_rawdata_generic(data_list);
}

void Wea1610::read_data(ModbusClient& mb_client) 
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
        LOG_ERROR_LOC("Modbus read error for Wea1610 " + get_name() + ": " + std::string(e.what()));
        safe_set_qt_data(false);
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}