#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"
#include <thread>
#include <chrono>

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

EjPcs15AmCmd::EjPcs15AmCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map) 
: modbus_clients_(modbus_clients)
,device_map_(device_map) {
}

// PCS开关机 (地址: 24586)
void EjPcs15AmCmd::pcs_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device) {
    if (switch_state != "on" && switch_state != "off") {
        LOG_WARNING_LOC("Invalid switch state: " + switch_state + ". Expected 'on' or 'off'.");
        return;
    }
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = (switch_state == "on") ? 1 : 0;
    
    // 发送两次开机/关机指令（参考Python代码）
    device->writeCmdToDevice(mb_client,"06",24586,value);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    device->writeCmdToDevice(mb_client,"06",24586,value);
    
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 开关机指令:" + switch_state + " 已发送");
}

// 复位PCS - AC部分
void EjPcs15AmCmd::pcs_reset_ac(std::shared_ptr<Device>& device,const std::string& mode) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    // AC复位: 写入电量清零指令 (地址: 28956)
    device->writeCmdToDevice(mb_client,"06",28956,1);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " AC复位指令已发送");
}

// 复位PCS - DC部分
void EjPcs15AmCmd::pcs_reset_dc(std::shared_ptr<Device>& device,const std::string& mode) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    // DC复位: 通过控制配置字实现 (地址: 24597)
    device->writeCmdToDevice(mb_client,"06",24597,1);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " DC复位指令已发送");
}

// 复位PCS (先AC后DC)
void EjPcs15AmCmd::pcs_reset(std::shared_ptr<Device>& device,const std::string& mode) {
    pcs_reset_ac(device, mode);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    pcs_reset_dc(device, mode);
}

// 固化参数 (地址: 28938)
void EjPcs15AmCmd::pcs_solid_param(const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28938,1);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 固化参数指令已发送");
}

// 设置DC部分充放电功率 (地址: 24582, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_dc_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",24582,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC功率: " + std::to_string(power) + "kW");
}

// 设置DC部分充放电电流 (地址: 24583, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_dc_current(const double& current,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(current * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",24583,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC电流: " + std::to_string(current) + "A");
}

// 设置DC部分最大功率 (地址: 24589, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_dc_max_power(const double& max_power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(max_power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",24589,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC最大功率: " + std::to_string(max_power) + "kW");
}



// 设置DC部分最大电流 (地址: 24590, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_dc_max_current(const double& max_current,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(max_current * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",24590,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC最大电流: " + std::to_string(max_current) + "A");
}



// 设置有功功率 (地址: 28934, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28934,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置有功功率: " + std::to_string(power) + "kW");
}

// 设置A相有功功率 (地址: 28948, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_phaseA_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 获取A相有功功率: " + std::to_string(raw_value));
    device->writeCmdToDevice(mb_client,"06",28948,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置A相有功功率: " + std::to_string(power) + "kW");
}

// 设置B相有功功率 (地址: 28950, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_phaseB_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28950,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置B相有功功率: " + std::to_string(power) + "kW");
}

// 设置C相有功功率 (地址: 28952, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_phaseC_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28952,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置C相有功功率: " + std::to_string(power) + "kW");
}

// 设置无功功率 (地址: 28935, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(reactive_power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28935,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置无功功率: " + std::to_string(reactive_power) + "kvar");
}

// 设置A相无功功率 (地址: 28949, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_phaseA_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(reactive_power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28949,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置A相无功功率: " + std::to_string(reactive_power) + "kvar");
}

// 设置B相无功功率 (地址: 28951, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_phaseB_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(reactive_power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28951,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置B相无功功率: " + std::to_string(reactive_power) + "kvar");
}

// 设置C相无功功率 (地址: 28953, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_phaseC_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(reactive_power * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28953,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置C相无功功率: " + std::to_string(reactive_power) + "kvar");
}

// 设置并离网模式 (地址: 28939, UINT16)
void EjPcs15AmCmd::pcs_switch_gridMode(const uint16_t& grid_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28939,grid_mode);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置并离网模式: " + std::to_string(grid_mode));
}

// 设置运行模式 (地址: 28940, UINT16)
void EjPcs15AmCmd::pcs_set_runMode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28940,run_mode);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置运行模式: " + std::to_string(run_mode));
}

// 设置单相独立控制 (地址: 28941, UINT16)
void EjPcs15AmCmd::pcs_set_singlePhaseCtrl(const uint16_t& single_phase_ctrl,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28941,single_phase_ctrl);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置单相独立控制: " + std::to_string(single_phase_ctrl));
}

// 设置主从模式 (地址: 28942, UINT16)
void EjPcs15AmCmd::pcs_set_masterMode(const uint16_t& master_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28942,master_mode);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置主从模式: " + std::to_string(master_mode));
}

// 设置直流源并联使能 (地址: 28943, UINT16)
void EjPcs15AmCmd::pcs_set_dcParallelEnable(const uint16_t& enable,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28943,enable);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置直流源并联使能: " + std::to_string(enable));
}

// 设置除湿使能 (地址: 28944, UINT16)
void EjPcs15AmCmd::pcs_set_dehumidify_enable(const uint16_t& enable,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28944,enable);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置除湿使能: " + std::to_string(enable));
}

// 设置离网电压 (地址: 28946, INT16, mag=1)
void EjPcs15AmCmd::pcs_set_offGridVol(const uint16_t& off_grid_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28946,off_grid_vol);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置离网电压: " + std::to_string(off_grid_vol) + "V");
}

// 设置离网频率 (地址: 28947, INT16, mag=100)
void EjPcs15AmCmd::pcs_set_frequency(const double& frequency,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(frequency * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",28947,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置离网频率: " + std::to_string(frequency) + "Hz");
}

// 设置DC源母线电压 (地址: 28954, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_dcBusVol(const uint16_t& dc_bus_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = dc_bus_vol * 10;
    device->writeCmdToDevice(mb_client,"06",28954,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC源母线电压: " + std::to_string(dc_bus_vol) + "V");
}

// 设置母线电压 (地址: 28955, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_chargeVol(const uint16_t& charge_vol,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = charge_vol * 10;
    device->writeCmdToDevice(mb_client,"06",28955,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置母线电压: " + std::to_string(charge_vol) + "V");
}

// 设置低压侧恒压充电电压 (地址: 24576, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_lv_charge_vol(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24576,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置低压侧恒压充电电压: " + std::to_string(voltage) + "V");
}

// 设置低压侧过压保护点 (地址: 24577, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_lv_over_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24577,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置低压侧过压保护: " + std::to_string(voltage) + "V");
}

// 设置低压侧欠压保护点 (地址: 24578, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_lv_under_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24578,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置低压侧欠压保护: " + std::to_string(voltage) + "V");
}

// 设置高压侧恒压充电电压 (地址: 24579, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_hv_charge_vol(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24579,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置高压侧恒压充电电压: " + std::to_string(voltage) + "V");
}

// 设置高压侧过压保护点 (地址: 24580, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_hv_over_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24580,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置高压侧过压保护: " + std::to_string(voltage) + "V");
}

// 设置高压侧欠压保护点 (地址: 24581, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_hv_under_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24581,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置高压侧欠压保护: " + std::to_string(voltage) + "V");
}

// 设置控制配置字 (地址: 24597, UINT16)
void EjPcs15AmCmd::pcs_set_cmdCfg(const uint16_t& cmd_cfg,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24597,cmd_cfg);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置控制配置字: " + std::to_string(cmd_cfg));
}

// 设置绝缘检测EMS控制单次检测使能 (地址: 24598, INT16)
void EjPcs15AmCmd::pcs_set_insulationTest_start(const uint16_t& start,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24598,start);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置绝缘检测启动: " + std::to_string(start));
}

// 设置绝缘检测启动电压 (地址: 24599, INT16, mag=10)
void EjPcs15AmCmd::pcs_set_insulationTest_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    uint16_t value = voltage * 10;
    device->writeCmdToDevice(mb_client,"06",24599,value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置绝缘检测电压: " + std::to_string(voltage) + "V");
}

// 设置绝缘检测使能 (地址: 24600, INT16)
void EjPcs15AmCmd::pcs_set_insulationTest_enable(const uint16_t& enable,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24600,enable);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置绝缘检测使能: " + std::to_string(enable));
}

// 设置DC快速模式 (地址: 24605, INT16)
void EjPcs15AmCmd::pcs_set_quick_dc_mode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24605,run_mode);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC快速模式: " + std::to_string(run_mode));
}

// 设置EPO故障触发(DC) (地址: 24606, UINT16)
void EjPcs15AmCmd::pcs_set_dc_epo(const uint16_t& epo,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",24606,epo);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置DC EPO: " + std::to_string(epo));
}

// 设置EPO故障触发(AC) (地址: 28957, UINT16)
void EjPcs15AmCmd::pcs_set_ac_epo(const uint16_t& epo,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    device->writeCmdToDevice(mb_client,"06",28957,epo);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置AC EPO: " + std::to_string(epo));
}

// 设置电网过频保护 (地址: 29056, UINT16, mag=100)
void EjPcs15AmCmd::pcs_set_gridOverFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(protection * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29056,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置电网过频保护: " + std::to_string(protection) + "Hz");
}

// 设置电网欠频保护 (地址: 29057, UINT16, mag=100)
void EjPcs15AmCmd::pcs_set_gridUnderFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(protection * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29057,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置电网欠频保护: " + std::to_string(protection) + "Hz");
}

// 设置孤岛过频保护 (地址: 29058, UINT16, mag=100)
void EjPcs15AmCmd::pcs_set_isolatedOverFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(protection * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29058,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置孤岛过频保护: " + std::to_string(protection) + "Hz");
}

// 设置孤岛欠频保护 (地址: 29059, UINT16, mag=100)
void EjPcs15AmCmd::pcs_set_isolatedUnderFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(protection * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29059,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置孤岛欠频保护: " + std::to_string(protection) + "Hz");
}

// 设置交流过压保护 (地址: 29060, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_acOverVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(voltage * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29060,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置交流过压保护: " + std::to_string(voltage) + "V");
}

// 设置交流欠压保护 (地址: 29061, UINT16, mag=10)
void EjPcs15AmCmd::pcs_set_acUnderVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(voltage * 10); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29061,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置交流欠压保护: " + std::to_string(voltage) + "V");
}

// 设置交流过流保护 (地址: 29062, UINT16)
void EjPcs15AmCmd::pcs_set_acOverCurrent_protection(const double& current,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(current); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29062,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置交流过流保护: " + std::to_string(current));
}

// 电池过压保护 (地址: 29063, UINT16)
void EjPcs15AmCmd::pcs_set_batOverVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(voltage * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29063,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置电池过压保护: " + std::to_string(voltage));
}

// 电池欠压保护 (地址: 29064, UINT16)
void EjPcs15AmCmd::pcs_set_batUnderVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(voltage); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29064,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置电池欠压保护: " + std::to_string(voltage));
}


void EjPcs15AmCmd::pcs_set_dcOverVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(voltage * 100); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29065,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置直流过压保护: " + std::to_string(voltage));
}


void EjPcs15AmCmd::pcs_set_dcUnderVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(voltage); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29066,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置直流欠压保护: " + std::to_string(voltage));
}

// 直流过流保护 (地址: 29067, UINT16)
void EjPcs15AmCmd::pcs_set_dcOverCurrent_protection(const double& current,const std::string& mode,std::shared_ptr<Device>& device) {
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(current); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29067,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置直流过流保护: " + std::to_string(current));
}


void EjPcs15AmCmd::pcs_set_moduleOverTemp_protection(const double& temp,const std::string& mode,std::shared_ptr<Device>& device) { 
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(temp); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29068,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置模块过温保护: " + std::to_string(temp));
}


void EjPcs15AmCmd::pcs_set_maxChargePower(const double& power,const std::string& mode,std::shared_ptr<Device>& device) { 
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29069,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置最大充电功率: " + std::to_string(power));
}


void EjPcs15AmCmd::pcs_set_maxDischargePower(const double& power,const std::string& mode,std::shared_ptr<Device>& device) { 
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(power); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29070,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置最大放电功率: " + std::to_string(power));
}


void EjPcs15AmCmd::pcs_set_batMaxChargeCurrent(const double& current,const std::string& mode,std::shared_ptr<Device>& device) { 
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(current); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29071,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置电池侧最大充电电流: " + std::to_string(current));
}

void EjPcs15AmCmd::pcs_set_batMaxDischargeCurrent(const double& current,const std::string& mode,std::shared_ptr<Device>& device) { 
    int com = device->get_com();
    std::shared_ptr<ModbusClient>& mb_client = this->modbus_clients_[com];
    int16_t raw_value_signed = static_cast<int16_t>(current); 
    uint16_t raw_value = static_cast<uint16_t>(raw_value_signed); 
    device->writeCmdToDevice(mb_client,"06",29072,raw_value);
    LOG_INFO_LOC("EJPCS15-AM " + device->get_name() + " 设置电池侧最大放电电流: " + std::to_string(current));
}



// 处理单个EJPCS15-AM设备的命令
void EjPcs15AmCmd::process_pcs_commands(const std::string& device_name) {
    try {
        auto& device_commands = cmd()->cmd_from_qt[device_name];
        
        // 检查设备是否存在
        if (device_map_.find(device_name) == device_map_.end()) {
            LOG_WARNING_LOC("Device " + device_name + " not found.");
            return;
        }
        std::shared_ptr<Device>& device = device_map_.at(device_name);
       
        if (device_commands.contains("on") && device_commands["on"].is_number() && device_commands["on"].get<int>()) {
            LOG_INFO_LOC("准备执行" + device_name + " on命令");
            pcs_on_off("on", "manual", device);
            device_commands["on"] = 0;
        }
        
        if (device_commands.contains("off") && device_commands["off"].is_number() && device_commands["off"].get<int>()) {
            LOG_INFO_LOC("准备执行" + device_name + " off命令");
            pcs_on_off("off", "manual", device);
            device_commands["off"] = 0;
        }
        
        // 处理复位命令
        if (device_commands.contains("setFaultReset") && device_commands["setFaultReset"].is_number() && device_commands["setFaultReset"].get<int>()) {
            LOG_INFO_LOC("准备执行" + device_name + " 复位命令");
            pcs_reset(device, "manual");
            device_commands["setFaultReset"] = false;
        }
        
        // 处理固化参数命令
        if (device_commands.contains("setSolidParam") && !device_commands["setSolidParam"].is_null()) {
            LOG_INFO_LOC("准备执行" + device_name + " 固化参数命令");
            pcs_solid_param("manual", device);
            device_commands["setSolidParam"] = nullptr;
        }
        
        // 处理DC功率设置
        if (device_commands.contains("setDcPower") && !device_commands["setDcPower"].is_null()) {
            int16_t power = device_commands["setDcPower"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置DC功率命令: " + std::to_string(power) + "kW");
            pcs_set_dc_power(power, "manual", device);
            device_commands["setDcPower"] = nullptr;
        }
        
        // 处理DC电流设置
        if (device_commands.contains("setDcCurrent") && !device_commands["setDcCurrent"].is_null()) {
            int16_t current = device_commands["setDcCurrent"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置DC电流命令: " + std::to_string(current) + "A");
            pcs_set_dc_current(current, "manual", device);
            device_commands["setDcCurrent"] = nullptr;
        }
        
        // 处理有功功率设置
        if (device_commands.contains("setPower") && !device_commands["setPower"].is_null()) {
            int16_t power = device_commands["setPower"].get<int16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置有功功率命令: " + std::to_string(power) + "kW");
            pcs_set_power(power, "manual", device);
            device_commands["setPower"] = nullptr;
        }
        
        // 处理A相有功功率设置
        if (device_commands.contains("setPhaseAPower") && !device_commands["setPhaseAPower"].is_null()) {
            double power = device_commands["setPhaseAPower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置A相有功功率命令: " + std::to_string(power) + "kW");
            pcs_set_phaseA_power(power, "manual", device);
            device_commands["setPhaseAPower"] = nullptr;
        }
        
        // 处理B相有功功率设置
        if (device_commands.contains("setPhaseBPower") && !device_commands["setPhaseBPower"].is_null()) {
            double power = device_commands["setPhaseBPower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置B相有功功率命令: " + std::to_string(power) + "kW");
            pcs_set_phaseB_power(power, "manual", device);
            device_commands["setPhaseBPower"] = nullptr;
        }
        
        // 处理C相有功功率设置
        if (device_commands.contains("setPhaseCPower") && !device_commands["setPhaseCPower"].is_null()) {
            double power = device_commands["setPhaseCPower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置C相有功功率命令: " + std::to_string(power) + "kW");
            pcs_set_phaseC_power(power, "manual", device);
            device_commands["setPhaseCPower"] = nullptr;
        }
        
        
        // 处理并离网模式设置
        if (device_commands.contains("setGridMode") && !device_commands["setGridMode"].is_null()) {
            uint16_t grid_mode = device_commands["setGridMode"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置并离网模式命令: " + std::to_string(grid_mode));
            pcs_switch_gridMode(grid_mode, "manual", device);
            device_commands["setGridMode"] = nullptr;
        }
        
        // 处理运行模式设置
        if (device_commands.contains("setRunMode") && !device_commands["setRunMode"].is_null()) {
            uint16_t run_mode = device_commands["setRunMode"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置运行模式命令: " + std::to_string(run_mode));
            pcs_set_runMode(run_mode, "manual", device);
            device_commands["setRunMode"] = nullptr;
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
        
        // 处理直流源并联使能
        if (device_commands.contains("setDcParallelEnable") && !device_commands["setDcParallelEnable"].is_null()) {
            uint16_t enable = device_commands["setDcParallelEnable"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置直流源并联使能命令: " + std::to_string(enable));
            pcs_set_dcParallelEnable(enable, "manual", device);
            device_commands["setDcParallelEnable"] = nullptr;
        }
        
        // 处理除湿使能
        if (device_commands.contains("setDehumidifyEnable") && !device_commands["setDehumidifyEnable"].is_null()) {
            uint16_t enable = device_commands["setDehumidifyEnable"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置除湿使能命令: " + std::to_string(enable));
            pcs_set_dehumidify_enable(enable, "manual", device);
            device_commands["setDehumidifyEnable"] = nullptr;
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
            double frequency = device_commands["setFrequency"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置频率命令: " + std::to_string(frequency) + "Hz");
            pcs_set_frequency(frequency, "manual", device);
            device_commands["setFrequency"] = nullptr;
        }
        
        // 处理DC源母线电压设置
        if (device_commands.contains("setDcBusVol") && !device_commands["setDcBusVol"].is_null()) {
            uint16_t dc_bus_vol = device_commands["setDcBusVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置DC源母线电压命令: " + std::to_string(dc_bus_vol) + "V");
            pcs_set_dcBusVol(dc_bus_vol, "manual", device);
            device_commands["setDcBusVol"] = nullptr;
        }
        
        // 处理母线电压设置
        if (device_commands.contains("setChargeVol") && !device_commands["setChargeVol"].is_null()) {
            uint16_t charge_vol = device_commands["setChargeVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置母线电压命令: " + std::to_string(charge_vol) + "V");
            pcs_set_chargeVol(charge_vol, "manual", device);
            device_commands["setChargeVol"] = nullptr;
        }
        
        // 处理低压侧恒压充电电压设置
        if (device_commands.contains("setLvChargeVol") && !device_commands["setLvChargeVol"].is_null()) {
            uint16_t voltage = device_commands["setLvChargeVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置低压侧恒压充电电压命令: " + std::to_string(voltage) + "V");
            pcs_set_lv_charge_vol(voltage, "manual", device);
            device_commands["setLvChargeVol"] = nullptr;
        }
        
        // 处理低压侧过压保护设置
        if (device_commands.contains("setLvOverVoltage") && !device_commands["setLvOverVoltage"].is_null()) {
            uint16_t voltage = device_commands["setLvOverVoltage"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置低压侧过压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_lv_over_voltage(voltage, "manual", device);
            device_commands["setLvOverVoltage"] = nullptr;
        }
        
        // 处理低压侧欠压保护设置
        if (device_commands.contains("setLvUnderVoltage") && !device_commands["setLvUnderVoltage"].is_null()) {
            uint16_t voltage = device_commands["setLvUnderVoltage"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置低压侧欠压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_lv_under_voltage(voltage, "manual", device);
            device_commands["setLvUnderVoltage"] = nullptr;
        }
        
        // 处理高压侧恒压充电电压设置
        if (device_commands.contains("setHvChargeVol") && !device_commands["setHvChargeVol"].is_null()) {
            uint16_t voltage = device_commands["setHvChargeVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置高压侧恒压充电电压命令: " + std::to_string(voltage) + "V");
            pcs_set_hv_charge_vol(voltage, "manual", device);
            device_commands["setHvChargeVol"] = nullptr;
        }
        
        // 处理高压侧过压保护设置
        if (device_commands.contains("setHvOverVoltage") && !device_commands["setHvOverVoltage"].is_null()) {
            uint16_t voltage = device_commands["setHvOverVoltage"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置高压侧过压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_hv_over_voltage(voltage, "manual", device);
            device_commands["setHvOverVoltage"] = nullptr;
        }
        
        // 处理高压侧欠压保护设置
        if (device_commands.contains("setHvUnderVoltage") && !device_commands["setHvUnderVoltage"].is_null()) {
            uint16_t voltage = device_commands["setHvUnderVoltage"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置高压侧欠压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_hv_under_voltage(voltage, "manual", device);
            device_commands["setHvUnderVoltage"] = nullptr;
        }
        
        // 处理控制配置字设置
        if (device_commands.contains("setCmdCfg") && !device_commands["setCmdCfg"].is_null()) {
            uint16_t cmd_cfg = device_commands["setCmdCfg"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置控制配置字命令: " + std::to_string(cmd_cfg));
            pcs_set_cmdCfg(cmd_cfg, "manual", device);
            device_commands["setCmdCfg"] = nullptr;
        }
        
        // 处理绝缘检测启动
        if (device_commands.contains("setInsuTestStart") && !device_commands["setInsuTestStart"].is_null()) {
            uint16_t start = device_commands["setInsuTestStart"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置绝缘检测启动命令: " + std::to_string(start));
            pcs_set_insulationTest_start(start, "manual", device);
            device_commands["setInsuTestStart"] = nullptr;
        }
        
        // 处理绝缘检测电压设置
        if (device_commands.contains("setInsuTestVoltage") && !device_commands["setInsuTestVoltage"].is_null()) {
            uint16_t voltage = device_commands["setInsuTestVoltage"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置绝缘检测电压命令: " + std::to_string(voltage) + "V");
            pcs_set_insulationTest_voltage(voltage, "manual", device);
            device_commands["setInsuTestVoltage"] = nullptr;
        }
        
        // 处理绝缘检测使能
        if (device_commands.contains("setInsuTestEnable") && !device_commands["setInsuTestEnable"].is_null()) {
            uint16_t enable = device_commands["setInsuTestEnable"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置绝缘检测使能命令: " + std::to_string(enable));
            pcs_set_insulationTest_enable(enable, "manual", device);
            device_commands["setInsuTestEnable"] = nullptr;
        }
        
        // 处理DC快速模式设置
        if (device_commands.contains("setDcQuickCfg") && !device_commands["setDcQuickCfg"].is_null()) {
            uint16_t run_mode = device_commands["setDcQuickCfg"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置DC快速模式命令: " + std::to_string(run_mode));
            pcs_set_quick_dc_mode(run_mode, "manual", device);
            device_commands["setDcQuickCfg"] = nullptr;
        }
        
        // 处理DC EPO设置
        if (device_commands.contains("setDcEpo") && !device_commands["setDcEpo"].is_null()) {
            uint16_t epo = device_commands["setDcEpo"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置DC EPO命令: " + std::to_string(epo));
            pcs_set_dc_epo(epo, "manual", device);
            device_commands["setDcEpo"] = nullptr;
        }
        
        // 处理AC EPO设置
        if (device_commands.contains("setAcEpo") && !device_commands["setAcEpo"].is_null()) {
            uint16_t epo = device_commands["setAcEpo"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置AC EPO命令: " + std::to_string(epo));
            pcs_set_ac_epo(epo, "manual", device);
            device_commands["setAcEpo"] = nullptr;
        }
        
        
        // 处理有功功率设置
        if (device_commands.contains("setActivePower") && !device_commands["setActivePower"].is_null()) {
            double power = device_commands["setActivePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置有功功率命令: " + std::to_string(power) + "kW");
            pcs_set_power(power, "manual", device);
            device_commands["setActivePower"] = nullptr;
        }
        
        // 处理无功功率设置
        if (device_commands.contains("setReactivePower") && !device_commands["setReactivePower"].is_null()) {
            double reactive_power = device_commands["setReactivePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置无功功率命令: " + std::to_string(reactive_power) + "kvar");
            pcs_set_reactivePower(reactive_power, "manual", device);
            device_commands["setReactivePower"] = nullptr;
        }
        
        // 处理单相控制设置
        if (device_commands.contains("setSinglePhaseControl") && !device_commands["setSinglePhaseControl"].is_null()) {
            uint16_t single_phase_ctrl = device_commands["setSinglePhaseControl"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置单相控制命令: " + std::to_string(single_phase_ctrl));
            pcs_set_singlePhaseCtrl(single_phase_ctrl, "manual", device);
            device_commands["setSinglePhaseControl"] = nullptr;
        }
        
        // 处理离网频率设置
        if (device_commands.contains("setOffgridFreq") && !device_commands["setOffgridFreq"].is_null()) {
            double frequency = device_commands["setOffgridFreq"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置离网频率命令: " + std::to_string(frequency) + "Hz");
            pcs_set_frequency(frequency, "manual", device);
            device_commands["setOffgridFreq"] = nullptr;
        }
        
        // 处理A相无功功率设置
        if (device_commands.contains("setPhaseAReactivePower") && !device_commands["setPhaseAReactivePower"].is_null()) {
            double reactive_power = device_commands["setPhaseAReactivePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置A相无功功率命令: " + std::to_string(reactive_power) + "kvar");
            pcs_set_phaseA_reactivePower(reactive_power, "manual", device);
            device_commands["setPhaseAReactivePower"] = nullptr;
        }
        
        // 处理B相无功功率设置
        if (device_commands.contains("setPhaseBReactivePower") && !device_commands["setPhaseBReactivePower"].is_null()) {
            double reactive_power = device_commands["setPhaseBReactivePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置B相无功功率命令: " + std::to_string(reactive_power) + "kvar");
            pcs_set_phaseB_reactivePower(reactive_power, "manual", device);
            device_commands["setPhaseBReactivePower"] = nullptr;
        }
        
        // 处理C相无功功率设置
        if (device_commands.contains("setPhaseCReactivePower") && !device_commands["setPhaseCReactivePower"].is_null()) {
            double reactive_power = device_commands["setPhaseCReactivePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置C相无功功率命令: " + std::to_string(reactive_power) + "kvar");
            pcs_set_phaseC_reactivePower(reactive_power, "manual", device);
            device_commands["setPhaseCReactivePower"] = nullptr;
        }
        
        // 处理DC源母线电压设置（单个）
        if (device_commands.contains("setDcBusVolForOne") && !device_commands["setDcBusVolForOne"].is_null()) {
            uint16_t dc_bus_vol = device_commands["setDcBusVolForOne"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置DC源母线电压命令: " + std::to_string(dc_bus_vol) + "V");
            pcs_set_dcBusVol(dc_bus_vol, "manual", device);
            device_commands["setDcBusVolForOne"] = nullptr;
        }
        
        // 处理清除容量
        if (device_commands.contains("setClearCapacity") && !device_commands["setClearCapacity"].is_null()) {
            uint16_t clear = device_commands["setClearCapacity"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 清除容量命令: " + std::to_string(clear));
            device_commands["setClearCapacity"] = nullptr;
        }
        
        // 处理电网过频保护设置
        if (device_commands.contains("setGridOverFreq") && !device_commands["setGridOverFreq"].is_null()) {
            double protection = device_commands["setGridOverFreq"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电网过频保护命令: " + std::to_string(protection) + "Hz");
            pcs_set_gridOverFreq_protection(protection, "manual", device);
            device_commands["setGridOverFreq"] = nullptr;
        }
        
        // 处理电网欠频保护设置
        if (device_commands.contains("setGridUnderFreq") && !device_commands["setGridUnderFreq"].is_null()) {
            double protection = device_commands["setGridUnderFreq"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电网欠频保护命令: " + std::to_string(protection) + "Hz");
            pcs_set_gridUnderFreq_protection(protection, "manual", device);
            device_commands["setGridUnderFreq"] = nullptr;
        }
        
        // 处理孤岛过频保护设置
        if (device_commands.contains("setIsolateOverFreq") && !device_commands["setIsolateOverFreq"].is_null()) {
            double protection = device_commands["setIsolateOverFreq"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置孤岛过频保护命令: " + std::to_string(protection) + "Hz");
            pcs_set_isolatedOverFreq_protection(protection, "manual", device);
            device_commands["setIsolateOverFreq"] = nullptr;
        }
        
        // 处理孤岛欠频保护设置
        if (device_commands.contains("setIsolateUnderFreq") && !device_commands["setIsolateUnderFreq"].is_null()) {
            double protection = device_commands["setIsolateUnderFreq"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置孤岛欠频保护命令: " + std::to_string(protection) + "Hz");
            pcs_set_isolatedUnderFreq_protection(protection, "manual", device);
            device_commands["setIsolateUnderFreq"] = nullptr;
        }
        
        // 处理交流过压保护设置
        if (device_commands.contains("setAcOverVoltage") && !device_commands["setAcOverVoltage"].is_null()) {
            double voltage = device_commands["setAcOverVoltage"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置交流过压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_acOverVoltage_protection(voltage, "manual", device);
            device_commands["setAcOverVoltage"] = nullptr;
        }
        
        // 处理交流欠压保护设置
        if (device_commands.contains("setAcUnderVoltage") && !device_commands["setAcUnderVoltage"].is_null()) {
            double voltage = device_commands["setAcUnderVoltage"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置交流欠压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_acUnderVoltage_protection(voltage, "manual", device);
            device_commands["setAcUnderVoltage"] = nullptr;
        }
        
        // 处理交流过流保护设置
        if (device_commands.contains("setAcOverCurrent") && !device_commands["setAcOverCurrent"].is_null()) {
            double current = device_commands["setAcOverCurrent"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置交流过流保护命令: " + std::to_string(current) + "A");
            pcs_set_acOverCurrent_protection(current, "manual", device);
            device_commands["setAcOverCurrent"] = nullptr;
        }
        
        // 处理电池过压保护设置
        if (device_commands.contains("setBatOverVoltage") && !device_commands["setBatOverVoltage"].is_null()) {
            double voltage = device_commands["setBatOverVoltage"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电池过压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_batOverVoltage_protection(voltage, "manual", device);
            device_commands["setBatOverVoltage"] = nullptr;
        }
        
        // 处理电池欠压保护设置
        if (device_commands.contains("setBatUnderVoltage") && !device_commands["setBatUnderVoltage"].is_null()) {
            double voltage = device_commands["setBatUnderVoltage"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电池欠压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_batUnderVoltage_protection(voltage, "manual", device);
            device_commands["setBatUnderVoltage"] = nullptr;
        }
        
        // 处理母线过压保护设置
        if (device_commands.contains("setBusOverVoltage") && !device_commands["setBusOverVoltage"].is_null()) {
            double voltage = device_commands["setBusOverVoltage"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置母线过压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_dcOverVoltage_protection(voltage, "manual", device);
            device_commands["setBusOverVoltage"] = nullptr;
        }
        
        // 处理母线欠压保护设置
        if (device_commands.contains("setBusUnderVoltage") && !device_commands["setBusUnderVoltage"].is_null()) {
            double voltage = device_commands["setBusUnderVoltage"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置母线欠压保护命令: " + std::to_string(voltage) + "V");
            pcs_set_dcUnderVoltage_protection(voltage, "manual", device);
            device_commands["setBusUnderVoltage"] = nullptr;
        }
        
        // 处理直流过流保护设置
        if (device_commands.contains("setDcOverCurrent") && !device_commands["setDcOverCurrent"].is_null()) {
            double current = device_commands["setDcOverCurrent"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置直流过流保护命令: " + std::to_string(current) + "A");
            pcs_set_dcOverCurrent_protection(current, "manual", device);
            device_commands["setDcOverCurrent"] = nullptr;
        }
        
        // 处理过温保护设置
        if (device_commands.contains("setOverTemperature") && !device_commands["setOverTemperature"].is_null()) {
            double temperature = device_commands["setOverTemperature"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置过温保护命令: " + std::to_string(temperature) + "°C");
            pcs_set_moduleOverTemp_protection(temperature, "manual", device);
            device_commands["setOverTemperature"] = nullptr;
        }
        
        // 处理最大充电功率设置
        if (device_commands.contains("setMaxChargePower") && !device_commands["setMaxChargePower"].is_null()) {
            double power = device_commands["setMaxChargePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置最大充电功率命令: " + std::to_string(power) + "kW");
            pcs_set_maxChargePower(power, "manual", device);
            device_commands["setMaxChargePower"] = nullptr;
        }
        
        // 处理最大放电功率设置
        if (device_commands.contains("setMaxDischargePower") && !device_commands["setMaxDischargePower"].is_null()) {
            double power = device_commands["setMaxDischargePower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置最大放电功率命令: " + std::to_string(power) + "kW");
            pcs_set_maxDischargePower(power, "manual", device);
            device_commands["setMaxDischargePower"] = nullptr;
        }
        
        // 处理电池最大充电电流设置
        if (device_commands.contains("setBatMaxChargeCurrent") && !device_commands["setBatMaxChargeCurrent"].is_null()) {
            double current = device_commands["setBatMaxChargeCurrent"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电池最大充电电流命令: " + std::to_string(current) + "A");
            pcs_set_batMaxChargeCurrent(current, "manual", device);
            device_commands["setBatMaxChargeCurrent"] = nullptr;
        }
        
        // 处理电池最大放电电流设置
        if (device_commands.contains("setBatMaxDischargeCurrent") && !device_commands["setBatMaxDischargeCurrent"].is_null()) {
            double current = device_commands["setBatMaxDischargeCurrent"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置电池最大放电电流命令: " + std::to_string(current) + "A");
            pcs_set_batMaxDischargeCurrent(current, "manual", device);
            device_commands["setBatMaxDischargeCurrent"] = nullptr;
        }
        
        // 处理最大DC功率设置
        if (device_commands.contains("setMaxDcPower") && !device_commands["setMaxDcPower"].is_null()) {
            double power = device_commands["setMaxDcPower"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置最大DC功率命令: " + std::to_string(power) + "kW");
            pcs_set_maxChargePower(power, "manual", device);
            device_commands["setMaxDcPower"] = nullptr;
        }
        
        // 处理最大DC电流设置
        if (device_commands.contains("setMaxDcCurrent") && !device_commands["setMaxDcCurrent"].is_null()) {
            double current = device_commands["setMaxDcCurrent"].get<double>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置最大DC电流命令: " + std::to_string(current) + "A");
            pcs_set_maxDischargePower(current, "manual", device);
            device_commands["setMaxDcCurrent"] = nullptr;
        }
        
        // 处理EMS绝缘测试使能
        if (device_commands.contains("setEmsInsuTestEnable") && !device_commands["setEmsInsuTestEnable"].is_null()) {
            uint16_t enable = device_commands["setEmsInsuTestEnable"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置EMS绝缘测试使能命令: " + std::to_string(enable));
            pcs_set_insulationTest_enable(enable, "manual", device);
            device_commands["setEmsInsuTestEnable"] = nullptr;
        }
        
        // 处理绝缘测试电压设置
        if (device_commands.contains("setInsulTestVol") && !device_commands["setInsulTestVol"].is_null()) {
            uint16_t voltage = device_commands["setInsulTestVol"].get<uint16_t>();
            LOG_INFO_LOC("准备执行" + device_name + " 设置绝缘测试电压命令: " + std::to_string(voltage) + "V");
            pcs_set_insulationTest_voltage(voltage, "manual", device);
            device_commands["setInsulTestVol"] = nullptr;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing " + device_name + " commands: " + std::string(e.what()));
    }
}

// 所有手动操作
void EjPcs15AmCmd::pcs_manual_control(const std::string& device_name) {
    // 处理EJPCS15-AM设备命令
    process_pcs_commands(device_name);
}