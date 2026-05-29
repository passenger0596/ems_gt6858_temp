#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

EjPcsCmd::EjPcsCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
    // this->modbus_clients_ = modbus_clients;
    // this->device_map_ = device_map;
}


// PCS开关机
void EjPcsCmd::pcs_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device) {
    if (switch_state != "on" && switch_state != "off") {
        LOG_WARNING_LOC("Invalid switch state: " + switch_state + ". Expected 'on' or 'off'.");
        return;
    }
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = (switch_state == "on") ? 1 : 0;
    device->writeCmdToDevice(mb_client,"06",657,value);
    LOG_INFO_LOC("pcs1 命令已发送");
}

// 复位PCS
void EjPcsCmd::pcs_reset(std::shared_ptr<Device>& device,const std::string& mode) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",5120,32768);
}

// 切换并离网模式,1:并网，0：离网
void EjPcsCmd::pcs_switch_gridMode(const uint16_t& grid_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",20582,grid_mode);
}

// 固化参数
void EjPcsCmd::pcs_solid_param(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",5125,256);
}


// 设置有功功率
void EjPcsCmd::pcs_set_power(const int16_t& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t raw_value = static_cast<uint16_t>(power*10);  // 将有功功率乘以10并转换为无符号整数
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",3415,raw_value);
}

// 设置运行模式,1:电流源模式，0：直流电压源模式
void EjPcsCmd::pcs_set_runMode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",20507,run_mode);
}


// 设置直流充电电压
void EjPcsCmd::pcs_set_dcChargeVol(const uint16_t& dc_charge_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t raw_value = dc_charge_vol*10;  // 将直流充电电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",3344,raw_value);
}



// 设置无功功率
void EjPcsCmd::pcs_set_reactivePower(const int16_t& reactive_power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t raw_value = static_cast<uint16_t>(reactive_power*10);  // 将无功功率乘以10并转换为无符号整数
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",3416,raw_value);
}


// 设置单相控制
void EjPcsCmd::pcs_set_singlePhaseCtrl(const uint16_t& single_phase_ctrl,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",20576,single_phase_ctrl);
}


// 设置主从模式
void EjPcsCmd::pcs_set_masterMode(const uint16_t& master_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",20584,master_mode);
}


// 设置离网输出电压
void EjPcsCmd::pcs_set_offGridVol(const uint16_t& off_grid_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",8743,off_grid_vol);
}

// 设置频率
void EjPcsCmd::pcs_set_frequency(const uint16_t& frequency,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = frequency * 100;  // 频率乘以100
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",259,value);
}

// 设置直流母线电压
void EjPcsCmd::pcs_set_dcBusVol(const uint16_t& dc_bus_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = static_cast<uint16_t>(dc_bus_vol * 10);  // 直流母线电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",3350,value);
}

// 设置VSG使能
void EjPcsCmd::pcs_set_vsgEnable(const uint16_t& vsg_enable,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",25088,vsg_enable);
}

// 设置VSG时间常数
void EjPcsCmd::pcs_set_vsgTimeVar(const uint16_t& vsg_time_var,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",25089,vsg_time_var);
}

// 设置VSG阻尼系数
void EjPcsCmd::pcs_set_vsgDamp(const uint16_t& vsg_damp,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",25090,vsg_damp);
}

// 设置VSG一次调频系数
void EjPcsCmd::pcs_set_vsgPfr(const uint16_t& vsg_pfr,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",25091,vsg_pfr);
}

// 设置VSG调压系数
void EjPcsCmd::pcs_set_vsgRcoef(const uint16_t& vsg_rcoef,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",25092,vsg_rcoef);
}

// 设置VSG虚拟R电阻
void EjPcsCmd::pcs_set_vsgVirtualR(const uint16_t& vsg_virtual_r,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",25093,vsg_virtual_r);
}

// 设置母线过压保护点
void EjPcsCmd::pcs_set_busHighProtect(const uint16_t& bus_high_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = bus_high_protect * 10;  // 母线电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",5649,value);
}

// 设置母线欠压保护点
void EjPcsCmd::pcs_set_busLowProtect(const uint16_t& bus_low_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = bus_low_protect * 10;  // 母线电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",5650,value);
}

// 设置交流过压保护点
void EjPcsCmd::pcs_set_acHighProtect(const uint16_t& ac_high_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = ac_high_protect * 10;  // 交流电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",5632,value);
}

// 设置交流欠压保护点
void EjPcsCmd::pcs_set_acLowProtect(const uint16_t& ac_low_protect,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    uint16_t value = ac_low_protect * 10;  // 交流电压乘以10
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",5633,value);
}

// 所有手动操作
void EjPcsCmd::pcs_manual_control(const std::string& device_name) {
    // 处理pcs设备命令
    process_pcs_commands(device_name);
}

// 处理单个PCS设备的命令
void EjPcsCmd::process_pcs_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
        
        // 处理开关机命令
        if (device_commands.contains("on") && device_commands["on"].is_boolean() && device_commands["on"].get<bool>()) {
            LOG_INFO_LOC("准备执行" + device_name + " on命令");
            pcs_on_off("on", "manual", device);
            device_commands["on"] = false;
        }
        
        if (device_commands.contains("off") && device_commands["off"].is_boolean() && device_commands["off"].get<bool>()) {
            LOG_INFO_LOC("准备执行" + device_name + " off命令");
            pcs_on_off("off", "manual", device);
            device_commands["off"] = false;
        }
        
        // 处理复位命令
        if (device_commands.contains("reset") && device_commands["reset"].is_boolean() && device_commands["reset"].get<bool>()) {
            LOG_INFO_LOC("准备执行" + device_name + " 复位命令");
            pcs_reset(device, "manual");
            device_commands["reset"] = false;
        }
        
        // 处理固化参数命令
        if (device_commands.contains("solidParam") && device_commands["solidParam"].is_boolean() && device_commands["solidParam"].get<bool>()) {
            LOG_INFO_LOC("准备执行" + device_name + " 固化参数命令");
            pcs_solid_param("manual", device);
            device_commands["solidParam"] = false;
        }
        
        // 处理有功功率设置
        if (device_commands.contains("setPower") && !device_commands["setPower"].is_null()) {
            int16_t power = device_commands["setPower"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置有功功率命令: " + std::to_string(power) + "kW");
            pcs_set_power(power, "manual", device);
            device_commands["setPower"] = nullptr;
        }
        
        // 处理充电电压设置
        if (device_commands.contains("setChargeVol") && !device_commands["setChargeVol"].is_null()) {
            int16_t charge_vol = device_commands["setChargeVol"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置充电电压命令: " + std::to_string(charge_vol) + "V");
            pcs_set_dcChargeVol(charge_vol, "manual", device);
            device_commands["setChargeVol"] = nullptr;
        }
        
        // 处理运行模式设置
        if (device_commands.contains("setRunMode") && !device_commands["setRunMode"].is_null()) {
            int16_t run_mode = device_commands["setRunMode"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置运行模式命令: " + std::to_string(run_mode));
            pcs_set_runMode(run_mode, "manual", device);
            device_commands["setRunMode"] = nullptr; 
        }
        
        // 处理并离网模式设置
        if (device_commands.contains("setGridMode") && !device_commands["setGridMode"].is_null()) {
            uint16_t grid_mode = device_commands["setGridMode"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置并离网模式命令: " + std::to_string(grid_mode));
            pcs_switch_gridMode(grid_mode, "manual", device);
            device_commands["setGridMode"] = nullptr;
        }
        
        // 处理无功功率设置
        if (device_commands.contains("setQPower") && !device_commands["setQPower"].is_null()) {
            int16_t q_power = device_commands["setQPower"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置无功功率命令: " + std::to_string(q_power) + "kvar");
            pcs_set_reactivePower(q_power, "manual", device);
            device_commands["setQPower"] = nullptr;
        }
        
        // 处理单相控制设置
        if (device_commands.contains("setSinglePhaseCtrl") && !device_commands["setSinglePhaseCtrl"].is_null()) {
            uint16_t single_phase_ctrl = device_commands["setSinglePhaseCtrl"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置单相控制命令: " + std::to_string(single_phase_ctrl));
            pcs_set_singlePhaseCtrl(single_phase_ctrl, "manual", device);
            device_commands["setSinglePhaseCtrl"] = nullptr;
        }
        
        // 处理主从模式设置
        if (device_commands.contains("setMasterMode") && !device_commands["setMasterMode"].is_null()) {
            uint16_t master_mode = device_commands["setMasterMode"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置主从模式命令: " + std::to_string(master_mode));
            pcs_set_masterMode(master_mode, "manual", device);
            device_commands["setMasterMode"] = nullptr; 
        }
        
        // 处理离网电压设置
        if (device_commands.contains("setOffGridVol") && !device_commands["setOffGridVol"].is_null()) {
            uint16_t off_grid_vol = device_commands["setOffGridVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置离网电压命令: " + std::to_string(off_grid_vol) + "V");
            pcs_set_offGridVol(off_grid_vol, "manual", device);
            device_commands["setOffGridVol"] = nullptr;
        }
        
        // 处理频率设置
        if (device_commands.contains("setFrequency") && !device_commands["setFrequency"].is_null()) {
            uint16_t frequency = device_commands["setFrequency"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置频率命令: " + std::to_string(frequency) + "Hz");
            pcs_set_frequency(frequency, "manual", device);
            device_commands["setFrequency"] = nullptr;
        }
        
        // 处理直流母线电压设置
        if (device_commands.contains("setDcBusVol") && !device_commands["setDcBusVol"].is_null()) {
            uint16_t dc_bus_vol = device_commands["setDcBusVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置直流母线电压命令: " + std::to_string(dc_bus_vol) + "V");
            pcs_set_dcBusVol(dc_bus_vol, "manual", device);
            device_commands["setDcBusVol"] = nullptr;
        }
        
        // 处理VSG相关设置
        if (device_commands.contains("setVsgEnable") && !device_commands["setVsgEnable"].is_null()) {
            uint16_t vsg_enable = device_commands["setVsgEnable"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置VSG使能命令: " + std::to_string(vsg_enable));
            pcs_set_vsgEnable(vsg_enable, "manual", device);
            device_commands["setVsgEnable"] = nullptr;
        }
        
        if (device_commands.contains("setVsgTimeVar") && !device_commands["setVsgTimeVar"].is_null()) {
            uint16_t vsg_time_var = device_commands["setVsgTimeVar"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置VSG时间常数命令: " + std::to_string(vsg_time_var));
            pcs_set_vsgTimeVar(vsg_time_var, "manual", device);
            device_commands["setVsgTimeVar"] = nullptr;
        }
        
        if (device_commands.contains("setVsgDamp") && !device_commands["setVsgDamp"].is_null()) {
            uint16_t vsg_damp = device_commands["setVsgDamp"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置VSG阻尼系数命令: " + std::to_string(vsg_damp));
            pcs_set_vsgDamp(vsg_damp, "manual", device);
            device_commands["setVsgDamp"] = nullptr;
        }
        
        if (device_commands.contains("setVsgPfr") && !device_commands["setVsgPfr"].is_null()) {
            uint16_t vsg_pfr = device_commands["setVsgPfr"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置VSG一次调频系数命令: " + std::to_string(vsg_pfr));
            pcs_set_vsgPfr(vsg_pfr, "manual", device);
            device_commands["setVsgPfr"] = nullptr;
        }
        
        if (device_commands.contains("setVsgRcoef") && !device_commands["setVsgRcoef"].is_null()) {
            uint16_t vsg_rcoef = device_commands["setVsgRcoef"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置VSG虚拟R系数命令: " + std::to_string(vsg_rcoef));
            pcs_set_vsgRcoef(vsg_rcoef, "manual", device);
            device_commands["setVsgRcoef"] = nullptr;
        }
        
        if (device_commands.contains("setVsgVirtualR") && !device_commands["setVsgVirtualR"].is_null()) {
            uint16_t vsg_virtual_r = device_commands["setVsgVirtualR"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置VSG虚拟R命令: " + std::to_string(vsg_virtual_r));
            pcs_set_vsgVirtualR(vsg_virtual_r, "manual", device);
            device_commands["setVsgVirtualR"] = nullptr;
        }
        
        // 处理保护设置
        if (device_commands.contains("setBusHighProtect") && !device_commands["setBusHighProtect"].is_null()) {
            uint16_t bus_high_protect = device_commands["setBusHighProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置母线过压保护命令: " + std::to_string(bus_high_protect) + "V");
            pcs_set_busHighProtect(bus_high_protect, "manual", device);
            device_commands["setBusHighProtect"] = nullptr;
        }
        
        if (device_commands.contains("setBusLowProtect") && !device_commands["setBusLowProtect"].is_null()) {
            uint16_t bus_low_protect = device_commands["setBusLowProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置母线欠压保护命令: " + std::to_string(bus_low_protect) + "V");
            pcs_set_busLowProtect(bus_low_protect, "manual", device);
            device_commands["setBusLowProtect"] = nullptr;
        }
        
        if (device_commands.contains("setAcHighProtect") && !device_commands["setAcHighProtect"].is_null()) {
            uint16_t ac_high_protect = device_commands["setAcHighProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置交流过压保护命令: " + std::to_string(ac_high_protect) + "V");
            pcs_set_acHighProtect(ac_high_protect, "manual", device);
            device_commands["setAcHighProtect"] = nullptr;
        }
        
        if (device_commands.contains("setAcLowProtect") && !device_commands["setAcLowProtect"].is_null()) {
            uint16_t ac_low_protect = device_commands["setAcLowProtect"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置交流欠压保护命令: " + std::to_string(ac_low_protect) + "V");
            pcs_set_acLowProtect(ac_low_protect, "manual", device);
            device_commands["setAcLowProtect"] = nullptr;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}