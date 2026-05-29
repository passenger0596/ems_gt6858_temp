#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

EjDcdcCmd::EjDcdcCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
    // this->modbus_clients_ = modbus_clients;
    // this->device_map_ = device_map;
}

// DCDC开关机
void EjDcdcCmd::dcdc_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device) {
    if (switch_state != "on" && switch_state != "off") {
        LOG_WARNING_LOC("Invalid switch state: " + switch_state + ". Expected 'on' or 'off'.");
        return;
    }
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = (switch_state == "on") ? 1 : 0;
    device->writeCmdToDevice(mb_client,"06",24842,value);
    LOG_INFO_LOC("DCDC " + switch_state + "命令已发送");
}

// 复位DCDC
void EjDcdcCmd::dcdc_reset(std::shared_ptr<Device>& device,const std::string& mode) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24847,32768);
}

// 固化参数
void EjDcdcCmd::dcdc_solid_param(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24843,256);
}

// 设置电流
void EjDcdcCmd::dcdc_set_current(const int16_t& current,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t raw_value = static_cast<uint16_t>(current * 10);  // 电流乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24839,raw_value);
}

// 设置功率
void EjDcdcCmd::dcdc_set_power(const int16_t& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t raw_value = static_cast<uint16_t>(power * 10);  // 功率乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24838,raw_value);
}

// 设置运行模式
void EjDcdcCmd::dcdc_set_runMode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24861,run_mode);
}

// 设置高压侧过压保护
void EjDcdcCmd::dcdc_setHVhighProtect(const uint16_t& hv_high_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = hv_high_protect * 10;  // 电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24836,value);
}

// 设置高压侧欠压保护
void EjDcdcCmd::dcdc_setHVlowProtect(const uint16_t& hv_low_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = hv_low_protect * 10;  // 电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24837,value);
}

// 设置低压侧过压保护
void EjDcdcCmd::dcdc_setLVhighProtect(const uint16_t& lv_high_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = lv_high_protect * 10;  // 电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24833,value);
}

// 设置低压侧欠压保护
void EjDcdcCmd::dcdc_setLVlowProtect(const uint16_t& lv_low_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = lv_low_protect * 10;  // 电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24834,value);
}

// 设置高压侧充电电压
void EjDcdcCmd::dcdc_setHVchargeVol(const uint16_t& hv_charge_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = hv_charge_vol * 10;  // 电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24835,value);
}

// 设置低压侧充电电压
void EjDcdcCmd::dcdc_setLVchargeVol(const uint16_t& lv_charge_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = lv_charge_vol * 10;  // 电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24832,value);
}

// 设置主从模式
void EjDcdcCmd::dcdc_setMasterMode(const uint16_t& master_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24860,master_mode);
}

// 设置自启动使能
void EjDcdcCmd::dcdc_enableAutoStart(const uint16_t& auto_start_enable,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24841,auto_start_enable);
}

// 设置并机数量
void EjDcdcCmd::dcdc_setParallelNum(const uint16_t& parallel_num,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24859,parallel_num);
}

// 所有手动操作
void EjDcdcCmd::dcdc_manual_control(const std::string& device_name) {
    // 处理dcdc设备命令 
    process_dcdc_commands(device_name);
}

// 处理单个DCDC设备的命令
void EjDcdcCmd::process_dcdc_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        // 处理开关机命令
        if (device_commands.contains("on") && device_commands["on"].is_number_integer() && device_commands["on"].get<int>() == 1) {
            LOG_INFO_LOC("准备执行" + device_name + " on命令");
            dcdc_on_off("on", "manual", device);
            device_commands["on"] = 0;
        }
        
        if (device_commands.contains("off") && device_commands["off"].is_number_integer() && device_commands["off"].get<int>() == 1) {
            LOG_INFO_LOC("准备执行" + device_name + " off命令");
            dcdc_on_off("off", "manual", device);
            device_commands["off"] = 0;
        }
        
        // 处理复位命令
        if (device_commands.contains("reset") && device_commands["reset"].is_number_integer() && device_commands["reset"].get<int>() == 1) {
            LOG_INFO_LOC("准备执行" + device_name + " 复位命令");
            dcdc_reset(device, "manual");
            device_commands["reset"] = 0;
        }
        
        // 处理固化参数命令
        if (device_commands.contains("solidParam") && device_commands["solidParam"].is_number_integer() && device_commands["solidParam"].get<int>() == 1) {
            LOG_INFO_LOC("准备执行" + device_name + " 固化参数命令");
            dcdc_solid_param("manual", device);
            device_commands["solidParam"] = 0;
        }
        
        // 处理电流设置
        if (device_commands.contains("setCurrent") && !device_commands["setCurrent"].is_null()) {
            int16_t current = device_commands["setCurrent"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电流命令: " + std::to_string(current) + "A");
            dcdc_set_current(current, "manual", device);
            device_commands["setCurrent"] = nullptr;
        }
        
        // 处理功率设置
        if (device_commands.contains("setPower") && !device_commands["setPower"].is_null()) {
            int16_t power = device_commands["setPower"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置功率命令: " + std::to_string(power) + "kW");
            dcdc_set_power(power, "manual", device);
            device_commands["setPower"] = nullptr;
        }
        
        // 处理运行模式设置
        if (device_commands.contains("setRunMode") && !device_commands["setRunMode"].is_null()) {
            uint16_t run_mode = device_commands["setRunMode"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置运行模式命令: " + std::to_string(run_mode));
            dcdc_set_runMode(run_mode, "manual", device);
            device_commands["setRunMode"] = nullptr;
        }
        
        // 处理高压侧过压保护设置
        if (device_commands.contains("setHVhighProtect") && !device_commands["setHVhighProtect"].is_null()) {
            uint16_t hv_high_protect = device_commands["setHVhighProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置高压侧过压保护命令: " + std::to_string(hv_high_protect) + "V");
            dcdc_setHVhighProtect(hv_high_protect, "manual", device);
            device_commands["setHVhighProtect"] = nullptr;
        }
        
        // 处理高压侧欠压保护设置
        if (device_commands.contains("setHVlowProtect") && !device_commands["setHVlowProtect"].is_null()) {
            uint16_t hv_low_protect = device_commands["setHVlowProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置高压侧欠压保护命令: " + std::to_string(hv_low_protect) + "V");
            dcdc_setHVlowProtect(hv_low_protect, "manual", device);
            device_commands["setHVlowProtect"] = nullptr;
        }
        
        // 处理低压侧过压保护设置
        if (device_commands.contains("setLVhighProtect") && !device_commands["setLVhighProtect"].is_null()) {
            uint16_t lv_high_protect = device_commands["setLVhighProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置低压侧过压保护命令: " + std::to_string(lv_high_protect) + "V");
            dcdc_setLVhighProtect(lv_high_protect, "manual", device);
            device_commands["setLVhighProtect"] = nullptr;
        }
        
        // 处理低压侧欠压保护设置
        if (device_commands.contains("setLVlowProtect") && !device_commands["setLVlowProtect"].is_null()) {
            uint16_t lv_low_protect = device_commands["setLVlowProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置低压侧欠压保护命令: " + std::to_string(lv_low_protect) + "V");
            dcdc_setLVlowProtect(lv_low_protect, "manual", device);
            device_commands["setLVlowProtect"] = nullptr;
        }
        
        // 处理高压侧充电电压设置
        if (device_commands.contains("setHVchargeVol") && !device_commands["setHVchargeVol"].is_null()) {
            uint16_t hv_charge_vol = device_commands["setHVchargeVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置高压侧充电电压命令: " + std::to_string(hv_charge_vol) + "V");
            dcdc_setHVchargeVol(hv_charge_vol, "manual", device);
            device_commands["setHVchargeVol"] = nullptr;
        }
        
        // 处理低压侧充电电压设置
        if (device_commands.contains("setLVchargeVol") && !device_commands["setLVchargeVol"].is_null()) {
            uint16_t lv_charge_vol = device_commands["setLVchargeVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置低压侧充电电压命令: " + std::to_string(lv_charge_vol) + "V");
            dcdc_setLVchargeVol(lv_charge_vol, "manual", device);
            device_commands["setLVchargeVol"] = nullptr;
        }
        
        // 处理主从模式设置
        if (device_commands.contains("setMaster") && !device_commands["setMaster"].is_null()) {
            uint16_t master_mode = device_commands["setMaster"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置主从模式命令: " + std::to_string(master_mode));
            dcdc_setMasterMode(master_mode, "manual", device);
            device_commands["setMaster"] = nullptr;
        }
        
        // 处理自启动使能设置
        if (device_commands.contains("enableAutoStart") && !device_commands["enableAutoStart"].is_null()) {
            uint16_t auto_start_enable = device_commands["enableAutoStart"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置自启动使能命令: " + std::to_string(auto_start_enable));
            dcdc_enableAutoStart(auto_start_enable, "manual", device);
            device_commands["enableAutoStart"] = nullptr;
        }
        
        // 处理并机数量设置
        if (device_commands.contains("setParallelNum") && !device_commands["setParallelNum"].is_null()) {
            uint16_t parallel_num = device_commands["setParallelNum"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置并机数量命令: " + std::to_string(parallel_num));
            dcdc_setParallelNum(parallel_num, "manual", device);
            device_commands["setParallelNum"] = nullptr;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}