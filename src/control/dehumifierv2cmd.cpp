#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;

DehumifierV2Cmd::DehumifierV2Cmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
}

void DehumifierV2Cmd::set_humidity_action_value(const uint16_t& humidity_value,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t raw_value = humidity_value * 10;
    device->writeCmdToDevice(mb_client,"06",0,raw_value);
    LOG_INFO_LOC("除湿机 " + device->get_name() + " 设置湿度动作值:" + std::to_string(humidity_value) + "%，模式:" + mode);
}

void DehumifierV2Cmd::set_humidity_stop_value(const uint16_t& humidity_value,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t raw_value = humidity_value * 10;
    device->writeCmdToDevice(mb_client,"06",1,raw_value);
    LOG_INFO_LOC("除湿机 " + device->get_name() + " 设置湿度停止值:" + std::to_string(humidity_value) + "%，模式:" + mode);
}

void DehumifierV2Cmd::set_comm_address(const uint16_t& address,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",2,address);
    LOG_INFO_LOC("除湿机 " + device->get_name() + " 设置通讯地址:" + std::to_string(address) + "，模式:" + mode);
}

void DehumifierV2Cmd::set_baud_rate(const uint16_t& baud_rate,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",3,baud_rate);
    LOG_INFO_LOC("除湿机 " + device->get_name() + " 设置波特率:" + std::to_string(baud_rate) + "，模式:" + mode);
}

void DehumifierV2Cmd::set_manual_auto_mode(const uint16_t& mode_value,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",4,mode_value);
    LOG_INFO_LOC("除湿机 " + device->get_name() + " 设置手动自动模式:" + std::to_string(mode_value) + "，模式:" + mode);
}

void DehumifierV2Cmd::process_dehumifier_v2_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        if (device_commands.contains("setHumidityActionValue") && !device_commands["setHumidityActionValue"].is_null()) {
            uint16_t humidity_value = device_commands["setHumidityActionValue"].get<uint16_t>();
            LOG_INFO_LOC("准备执行 " + device_name + " 设置湿度动作值命令: " + std::to_string(humidity_value) + "%");
            set_humidity_action_value(humidity_value, "manual", device);
            device_commands["setHumidityActionValue"] = nullptr;
        }
        
        if (device_commands.contains("setHumidityStopValue") && !device_commands["setHumidityStopValue"].is_null()) {
            uint16_t humidity_value = device_commands["setHumidityStopValue"].get<uint16_t>();
            LOG_INFO_LOC("准备执行 " + device_name + " 设置湿度停止值命令: " + std::to_string(humidity_value) + "%");
            set_humidity_stop_value(humidity_value, "manual", device);
            device_commands["setHumidityStopValue"] = nullptr;
        }
        
        if (device_commands.contains("setCommAddress") && !device_commands["setCommAddress"].is_null()) {
            uint16_t address = device_commands["setCommAddress"].get<uint16_t>();
            LOG_INFO_LOC("准备执行 " + device_name + " 设置通讯地址命令: " + std::to_string(address));
            set_comm_address(address, "manual", device);
            device_commands["setCommAddress"] = nullptr;
        }
        
        if (device_commands.contains("setBaudRate") && !device_commands["setBaudRate"].is_null()) {
            uint16_t baud_rate = device_commands["setBaudRate"].get<uint16_t>();
            LOG_INFO_LOC("准备执行 " + device_name + " 设置波特率命令: " + std::to_string(baud_rate));
            set_baud_rate(baud_rate, "manual", device);
            device_commands["setBaudRate"] = nullptr;
        }
        
        if (device_commands.contains("setManualAutoMode") && !device_commands["setManualAutoMode"].is_null()) {
            uint16_t mode_value = device_commands["setManualAutoMode"].get<uint16_t>();
            LOG_INFO_LOC("准备执行 " + device_name + " 设置手动自动模式命令: " + std::to_string(mode_value));
            set_manual_auto_mode(mode_value, "manual", device);
            device_commands["setManualAutoMode"] = nullptr;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}

void DehumifierV2Cmd::dehumifier_v2_manual_control(const std::string& device_name) {
    process_dehumifier_v2_commands(device_name);
}