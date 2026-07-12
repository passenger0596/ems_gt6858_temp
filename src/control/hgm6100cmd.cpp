#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;

Hgm6100Cmd::Hgm6100Cmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
}

void Hgm6100Cmd::dg_on(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"05",0,1);
    LOG_INFO_LOC("柴发 " + device->get_name() + " 开机指令，模式:" + mode);
}

void Hgm6100Cmd::dg_off(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"05",1,1);
    LOG_INFO_LOC("柴发 " + device->get_name() + " 关机指令，模式:" + mode);
}

void Hgm6100Cmd::dg_set_auto_mode(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"05",3,1);
    LOG_INFO_LOC("柴发 " + device->get_name() + " 自动模式指令，模式:" + mode);
}

void Hgm6100Cmd::dg_set_manual_mode(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"05",4,1);
    LOG_INFO_LOC("柴发 " + device->get_name() + " 手动模式指令，模式:" + mode);
}

void Hgm6100Cmd::process_dg_hgm6100n_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_ERROR_LOC("Device" + device_name + " not found");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        if (device_commands.contains("on") && device_commands["on"].is_boolean() && device_commands["on"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 开机命令");
            dg_set_manual_mode("手动", device);
            dg_on("手动", device);
            device_commands["on"] = false;
        }
        
        if (device_commands.contains("off") && device_commands["off"].is_boolean() && device_commands["off"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 关机命令");
            dg_set_manual_mode("手动", device);
            dg_off("手动", device);
            device_commands["off"] = false;
        }
        
        if (device_commands.contains("setAutoMode") && device_commands["setAutoMode"].is_boolean() && device_commands["setAutoMode"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 自动模式命令");
            dg_set_auto_mode("手动", device);
            device_commands["setAutoMode"] = false;
        }
        
        if (device_commands.contains("setManualMode") && device_commands["setManualMode"].is_boolean() && device_commands["setManualMode"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 手动模式命令");
            dg_set_manual_mode("手动", device);
            device_commands["setManualMode"] = false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}

void Hgm6100Cmd::dg_hgm6100n_manual_control(const std::string& device_name) {
    process_dg_hgm6100n_commands(device_name);
}