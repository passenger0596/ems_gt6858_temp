#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

KndIoMuduleCmd::KndIoMuduleCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {

}


void KndIoMuduleCmd::io_module4040_operate(const std::string& switch_state,const int do_num,const std::string& mode,std::shared_ptr<Device>& device) {
    // 单个实现4040模块的IO操作
    std::shared_ptr<ModbusClient> modbus_client = modbus_clients_[device->get_com()];
    uint16_t value = switch_state == "close" ? 1 : 0;
    uint16_t addr;
    if (do_num==1) addr = 100;
    if (do_num==2) addr = 101;
    if (do_num==3) addr = 102;
    if (do_num==4) addr = 103;
    device->writeCmdToDevice(modbus_client,"05",addr,value);
    LOG_DEBUG_LOC("write coil " + std::to_string(addr) + " value " + std::to_string(value));
}


void KndIoMuduleCmd::io_module4040_multi_operate(const std::vector<uint16_t>& switch_list,const std::string& mode,std::shared_ptr<Device>& device) {
    // 批量实现4040模块的IO操作
    std::shared_ptr<ModbusClient> modbus_client = modbus_clients_[device->get_com()];
    device->multiWriteCmdToDevice(modbus_client,"15",100,switch_list);
    LOG_DEBUG_LOC("write coils " + std::to_string(switch_list.size()) + " values");
}


void KndIoMuduleCmd::io_module2080_operate(const std::string& switch_state,const int do_num,const std::string& mode,std::shared_ptr<Device>& device) {
    std::shared_ptr<ModbusClient> modbus_client = modbus_clients_[device->get_com()];
    uint16_t value = switch_state == "close" ? 1 : 0;
    uint16_t addr;
    if (do_num==1) addr = 100;
    if (do_num==2) addr = 101;
    if (do_num==3) addr = 102;
    if (do_num==4) addr = 103;
    if (do_num==5) addr = 104;
    if (do_num==6) addr = 105;
    if (do_num==7) addr = 106;
    if (do_num==8) addr = 107;
    device->writeCmdToDevice(modbus_client,"05",addr,value);
    LOG_DEBUG_LOC("write coil " + std::to_string(addr) + " value " + std::to_string(value));
}


void KndIoMuduleCmd::io_module2080_multi_operate(const std::vector<uint16_t>& switch_list,const std::string& mode,std::shared_ptr<Device>& device) {
    std::shared_ptr<ModbusClient> modbus_client = modbus_clients_[device->get_com()];
    device->multiWriteCmdToDevice(modbus_client,"15",100,switch_list);
    LOG_DEBUG_LOC("write coils " + std::to_string(switch_list.size()) + " values");
}


void KndIoMuduleCmd::process_iomodule4040_commands(const std::string& device_name){
    try{
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);

        for (int i=1;i<=4;i++){
            std::string key = "do" + std::to_string(i);
            if (device_commands.contains(key) && !device_commands[key].is_null()) {
                LOG_INFO_LOC("准备执行" + device_name + " do" + std::to_string(i) + "命令");
                std::string switch_state = device_commands[key].get<std::string>();
                io_module2080_operate(switch_state, i, "手动", device);
                device_commands[key] = nullptr;
            }
        }
    }catch(const std::exception& e){
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}


void KndIoMuduleCmd::process_iomodule2080_commands(const std::string& device_name){
    try{
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {   
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);

        for (int i=1;i<=8;i++){
            std::string key = "do" + std::to_string(i);
            if (device_commands.contains(key) && !device_commands[key].is_null()) {
                LOG_INFO_LOC("准备执行" + device_name + " do" + std::to_string(i) + "命令");
                std::string switch_state = device_commands[key].get<std::string>();
                io_module2080_operate(switch_state, i, "手动", device);
                device_commands[key] = nullptr;
            }
        }

    }catch(const std::exception& e){
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}
