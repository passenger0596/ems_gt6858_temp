#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

AcWea1610Cmd::AcWea1610Cmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
}

// 设置空调制冷参数 (地址: 4, 多个寄存器)
void AcWea1610Cmd::set_ac_cooler(const std::vector<uint16_t>& var_list,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    
    // 使用功能码16(写多个寄存器)发送参数列表
    device->multiWriteCmdToDevice(mb_client,"16",4,var_list);
    
    LOG_INFO_LOC("空调 " + device->get_name() + " 设置制冷参数成功，模式:" + mode);
}

// 设置制冷温度 (地址: 4, INT16)
void AcWea1610Cmd::set_ac_coolingTemp(const int16_t& coolingTemp,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    
    // 直接写入制冷温度值
    device->writeCmdToDevice(mb_client,"06",4,static_cast<uint16_t>(coolingTemp));
    
    LOG_INFO_LOC("空调 " + device->get_name() + " 设置制冷温度:" + std::to_string(coolingTemp) + "℃，模式:" + mode);
}

// 处理空调命令
void AcWea1610Cmd::process_ac_wea1610_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        // 处理设置制冷温度命令
        if (device_commands.contains("setCoolingTemp") && !device_commands["setCoolingTemp"].is_null()) {
            int16_t coolingTemp = device_commands["setCoolingTemp"].get<int16_t>();
            LOG_INFO_LOC("准备执行 " + device_name + " 设置制冷温度命令: " + std::to_string(coolingTemp) + "℃");
            set_ac_coolingTemp(coolingTemp, "manual", device);
            device_commands["setCoolingTemp"] = nullptr;
        }
        
        // 处理设置完整制冷参数命令
        if (device_commands.contains("setCooler") && device_commands["setCooler"].is_boolean() && device_commands["setCooler"].get<bool>()) {
            LOG_INFO_LOC("准备执行 " + device_name + " 设置制冷参数命令");
            
            // 从命令中获取各个参数
            int16_t coolingTemp = device_commands.contains("coolingTemp") ? device_commands["coolingTemp"].get<int16_t>() : 0;
            uint16_t tempReturnDiff = device_commands.contains("tempReturnDiff") ? device_commands["tempReturnDiff"].get<uint16_t>() : 0;
            uint16_t compressorDelayTime = device_commands.contains("compressorDelayTime") ? device_commands["compressorDelayTime"].get<uint16_t>() : 0;
            uint16_t forceCoolingTime = device_commands.contains("forceCoolingTime") ? device_commands["forceCoolingTime"].get<uint16_t>() : 0;
            int16_t tempRecalibration = device_commands.contains("tempRecalibration") ? device_commands["tempRecalibration"].get<int16_t>() : 0;
            uint16_t minTemp = device_commands.contains("minTemp") ? Utils::signed_to_unsigned(device_commands["minTemp"].get<int16_t>()) : 0;
            uint16_t maxTemp = device_commands.contains("maxTemp") ? device_commands["maxTemp"].get<uint16_t>() : 0;
            int16_t heatingTemp = device_commands.contains("heatingTemp") ? Utils::signed_to_unsigned(device_commands["heatingTemp"].get<int16_t>()) : 0;
            uint16_t overTempAlarm = device_commands.contains("overTempAlarm") ? device_commands["overTempAlarm"].get<uint16_t>() : 0;
            uint16_t underTempAlarm = device_commands.contains("underTempAlarm") ? Utils::signed_to_unsigned(device_commands["underTempAlarm"].get<int16_t>()) : 0;
            
            // 构建参数列表
            std::vector<uint16_t> value_list = {
                static_cast<uint16_t>(coolingTemp),
                tempReturnDiff,
                compressorDelayTime,
                forceCoolingTime,
                static_cast<uint16_t>(tempRecalibration),
                minTemp,
                maxTemp,
                static_cast<uint16_t>(heatingTemp),
                overTempAlarm,
                underTempAlarm
            };
            
            set_ac_cooler(value_list, "manual", device);
            device_commands["setCooler"] = false;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}

// 手动控制入口
void AcWea1610Cmd::ac_wea1610_manual_control(const std::string& device_name) {
    process_ac_wea1610_commands(device_name);
}
