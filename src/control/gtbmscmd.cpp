#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"
#include <thread>
#include <chrono>

static auto& cmd = QtController::getInstance;

GtbmsCmd::GtbmsCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
}

void GtbmsCmd::gtbms_vol_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    
    device->writeCmdToDevice(mb_client,"06",99,1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    uint16_t value = 0;
    if (switch_state == "on") {
        value = 85;
    } else if (switch_state == "off") {
        value = 170;
    }
    device->writeCmdToDevice(mb_client,"06",100,value);
    LOG_INFO_LOC("BMS " + device->get_name() + " " + switch_state + "高压，值:" + std::to_string(value) + "，模式:" + mode);
}

void GtbmsCmd::gtbms_reset_fault(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    
    device->writeCmdToDevice(mb_client,"06",108,1);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    device->writeCmdToDevice(mb_client,"06",108,0);
    LOG_INFO_LOC("BMS " + device->get_name() + " 故障复位，模式:" + mode);
}

void GtbmsCmd::gtbms_read_protection(std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    
    device->writeCmdToDevice(mb_client,"06",1000,1);
    LOG_INFO_LOC("BMS " + device->get_name() + " 发送读取保护参数指令");
}

void GtbmsCmd::process_gtbms_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_ERROR_LOC("Device" + device_name + " not found");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        if (device_commands.contains("highVoltageOn") && device_commands["highVoltageOn"].is_boolean() && device_commands["highVoltageOn"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 上高压命令");
            gtbms_vol_on_off("on", "手动", device);
            device_commands["highVoltageOn"] = false;
        }
        
        if (device_commands.contains("highVoltageOff") && device_commands["highVoltageOff"].is_boolean() && device_commands["highVoltageOff"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 下高压命令");
            gtbms_vol_on_off("off", "手动", device);
            device_commands["highVoltageOff"] = false;
        }
        
        if (device_commands.contains("bms_reset") && device_commands["bms_reset"].is_boolean() && device_commands["bms_reset"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 故障复位命令");
            gtbms_reset_fault("手动", device);
            device_commands["bms_reset"] = false;
        }
        
        if (device_commands.contains("setEnableReadProtection") && !device_commands["setEnableReadProtection"].is_null()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 读取保护参数命令");
            gtbms_read_protection(device);
            device_commands["setEnableReadProtection"] = nullptr;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}

void GtbmsCmd::gtbms_manual_control(const std::string& device_name) {
    process_gtbms_commands(device_name);
}