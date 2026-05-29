#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

// XMCooler类的构造函数
XmCoolerCmd::XmCoolerCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients, 
                        std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
    : modbus_clients_(modbus_clients), device_map_(device_map) {
}


// XM冷却器开关机控制
void XmCoolerCmd::xmcooler_on_off(const uint16_t& switch_state, const std::string& mode, 
                                  std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    
    // 根据鑫淼冷却器协议设置相应的寄存器地址
    device->writeCmdToDevice(mb_client, "06", 201, switch_state);  
    LOG_INFO_LOC("XMCooler " + std::to_string(switch_state) + "命令已发送");
}

void XmCoolerCmd::xmcooler_set_cooler(const uint16_t& cooler_mode,const uint16_t& on_off,const int16_t& set_temp,const std::string& mode,std::shared_ptr<Device>& device){
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    std::vector<uint16_t> value_list = {cooler_mode,on_off,0,0,0,static_cast<uint16_t>(set_temp*10)};
    device->multiWriteCmdToDevice(mb_client, "16", 200, value_list);
}

// XM冷却器设置温度
void XmCoolerCmd::xmcooler_set_temperature(const int16_t& temperature,const std::string& mode,std::shared_ptr<Device>& device){
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client, "06", 205, static_cast<uint16_t>(temperature*10));
    LOG_INFO_LOC("XMCooler " + std::to_string(temperature) + "命令已发送");
}
       

// 所有手动操作
void XmCoolerCmd::xm_cooler_manual_control(const std::string& device_name) {
    // 处理xm_cooler设备命令
    process_xm_cooler_commands(device_name);
}

// 处理单个XM冷却器设备的命令
void XmCoolerCmd::process_xm_cooler_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        if (device_commands.contains("setCooler") && device_commands["setCooler"].is_number_integer() && device_commands["setCooler"].get<int>() == 1) {
            LOG_INFO_LOC("准备执行" + device_name + " setCooler命令");
            
            // 检查所有必需的参数是否存在且不为 null
            if (device_commands.contains("setMode") && !device_commands["setMode"].is_null() &&
                device_commands.contains("on_off") && !device_commands["on_off"].is_null() &&
                device_commands.contains("setTemp") && !device_commands["setTemp"].is_null()) {
                
                uint16_t cooler_mode = device_commands["setMode"].get<uint16_t>();
                uint16_t on_off = device_commands["on_off"].get<uint16_t>();
                int16_t set_temp = device_commands["setTemp"].get<int16_t>();
                
                xmcooler_set_cooler(cooler_mode, on_off, set_temp, "手动", device);
                device_commands["setCooler"] = 0;
            } else {
                LOG_WARNING_LOC("缺少必要的参数(setMode/on_off/setTemp)，跳过setCooler命令");
            }
        }     

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}