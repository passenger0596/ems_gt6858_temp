#ifndef COMMAND_H
#define COMMAND_H

#include "device.h"
#include "modbusclient.h"
#include <unordered_map>
#include "ems.h"


class EjPcsCmd {
    public:
        EjPcsCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~EjPcsCmd()=default;
        void pcs_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_reset(std::shared_ptr<Device>& device,const std::string& mode);
        void pcs_switch_gridMode(const uint16_t& grid_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_solid_param(const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_power(const int16_t& power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_runMode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dcChargeVol(const uint16_t& dc_charge_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_reactivePower(const int16_t& reactive_power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_singlePhaseCtrl(const uint16_t& single_phase_ctrl,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_masterMode(const uint16_t& master_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_offGridVol(const uint16_t& off_grid_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_frequency(const uint16_t& frequency,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dcBusVol(const uint16_t& dc_bus_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_vsgEnable(const uint16_t& vsg_enable,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_vsgTimeVar(const uint16_t& vsg_time_var,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_vsgDamp(const uint16_t& vsg_damp,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_vsgPfr(const uint16_t& vsg_pfr,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_vsgRcoef(const uint16_t& vsg_rcoef,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_vsgVirtualR(const uint16_t& vsg_virtual_r,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_busHighProtect(const uint16_t& bus_high_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_busLowProtect(const uint16_t& bus_low_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_acHighProtect(const uint16_t& ac_high_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_acLowProtect(const uint16_t& ac_low_protect,const std::string& mode,std::shared_ptr<Device>& device);

        void process_pcs_commands(const std::string& device_name);
        void pcs_manual_control(const std::string& device_name);


    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  
};


class EjPcs15AmCmd {
    public:
        EjPcs15AmCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~EjPcs15AmCmd()=default;
        
        // 基础控制
        void pcs_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_reset(std::shared_ptr<Device>& device,const std::string& mode);
        void pcs_reset_ac(std::shared_ptr<Device>& device,const std::string& mode);
        void pcs_reset_dc(std::shared_ptr<Device>& device,const std::string& mode);
        void pcs_solid_param(const std::string& mode,std::shared_ptr<Device>& device);
        
        // DC部分功率和电流设置
        void pcs_set_dc_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dc_current(const double& current,const std::string& mode,std::shared_ptr<Device>& device);

        // DC部分最大功率和电流设置
        void pcs_set_dc_max_power(const double& max_power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dc_max_current(const double& max_current,const std::string& mode,std::shared_ptr<Device>& device);
        
        // AC部分功率设置
        void pcs_set_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_phaseA_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_phaseB_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_phaseC_power(const double& power,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 无功功率设置
        void pcs_set_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_phaseA_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_phaseB_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_phaseC_reactivePower(const double& reactive_power,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 运行模式设置
        void pcs_switch_gridMode(const uint16_t& grid_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_runMode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_singlePhaseCtrl(const uint16_t& single_phase_ctrl,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_masterMode(const uint16_t& master_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dcParallelEnable(const uint16_t& enable,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dehumidify_enable(const uint16_t& enable,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 电压和频率设置
        void pcs_set_offGridVol(const uint16_t& off_grid_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_frequency(const double& frequency,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dcBusVol(const uint16_t& dc_bus_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_chargeVol(const uint16_t& charge_vol,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 低压侧保护设置
        void pcs_set_lv_charge_vol(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_lv_over_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_lv_under_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 高压侧保护设置
        void pcs_set_hv_charge_vol(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_hv_over_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_hv_under_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 配置字和控制
        void pcs_set_cmdCfg(const uint16_t& cmd_cfg,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 绝缘测试
        void pcs_set_insulationTest_start(const uint16_t& start,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_insulationTest_voltage(const uint16_t& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_insulationTest_enable(const uint16_t& enable,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 快速模式和EPO
        void pcs_set_quick_dc_mode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dc_epo(const uint16_t& epo,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_ac_epo(const uint16_t& epo,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 频率保护设置
        void pcs_set_gridOverFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_gridUnderFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_isolatedOverFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_isolatedUnderFreq_protection(const double& protection,const std::string& mode,std::shared_ptr<Device>& device);

        // 交流过压欠压保护设置
        void pcs_set_acOverVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_acUnderVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device);

        // 交流侧过流保护设置
        void pcs_set_acOverCurrent_protection(const double& current,const std::string& mode,std::shared_ptr<Device>& device);

        // 电池侧过压欠压保护设置
        void pcs_set_batOverVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_batUnderVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device);
     
        // 直流过压欠压保护设置
        void pcs_set_dcOverVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_dcUnderVoltage_protection(const double& voltage,const std::string& mode,std::shared_ptr<Device>& device);

        // 直流侧过流保护设置
        void pcs_set_dcOverCurrent_protection(const double& current,const std::string& mode,std::shared_ptr<Device>& device);

        // 模块过温保护点设置
        void pcs_set_moduleOverTemp_protection(const double& temp,const std::string& mode,std::shared_ptr<Device>& device);

        // 最大充放电功率设置
        void pcs_set_maxChargePower(const double& power,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_maxDischargePower(const double& power,const std::string& mode,std::shared_ptr<Device>& device);

        // 电池侧最大充放电电流设置
        void pcs_set_batMaxChargeCurrent(const double& current,const std::string& mode,std::shared_ptr<Device>& device);
        void pcs_set_batMaxDischargeCurrent(const double& current,const std::string& mode,std::shared_ptr<Device>& device);

        void process_pcs_commands(const std::string& device_name);
        void pcs_manual_control(const std::string& device_name);


    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  
};


class EjDcdcCmd {
    public:
        EjDcdcCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~EjDcdcCmd()=default;
        void dcdc_on_off(const std::string& switch_state,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_reset(std::shared_ptr<Device>& device,const std::string& mode);
        void dcdc_solid_param(const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_set_current(const int16_t& current,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_set_power(const int16_t& power,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_set_runMode(const uint16_t& run_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setHVhighProtect(const uint16_t& hv_high_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setHVlowProtect(const uint16_t& hv_low_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setLVhighProtect(const uint16_t& lv_high_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setLVlowProtect(const uint16_t& lv_low_protect,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setHVchargeVol(const uint16_t& hv_charge_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setLVchargeVol(const uint16_t& lv_charge_vol,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setMasterMode(const uint16_t& master_mode,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_enableAutoStart(const uint16_t& auto_start_enable,const std::string& mode,std::shared_ptr<Device>& device);
        void dcdc_setParallelNum(const uint16_t& parallel_num,const std::string& mode,std::shared_ptr<Device>& device);
            
        void process_dcdc_commands(const std::string& device_name);
        void dcdc_manual_control(const std::string& device_name);



    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  
};

class XmCoolerCmd {
    public:
        XmCoolerCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~XmCoolerCmd()=default;
        void xmcooler_on_off(const uint16_t& switch_state,const std::string& mode,std::shared_ptr<Device>& device);
        void xmcooler_set_cooler(const uint16_t& cooler_mode,const uint16_t& on_off,const int16_t& set_temp,const std::string& mode,std::shared_ptr<Device>& device);
        void xmcooler_set_temperature(const int16_t& temperature,const std::string& mode,std::shared_ptr<Device>& device);

        void process_xm_cooler_commands(const std::string& device_name);
        void xm_cooler_manual_control(const std::string& device_name);
    
    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  
            
};


class AcWea1610Cmd {
    public:
        AcWea1610Cmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~AcWea1610Cmd()=default;
        
        // 设置空调制冷参数(地址: 4, 多个寄存器)
        void set_ac_cooler(const std::vector<uint16_t>& var_list,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 设置制冷温度 (地址: 4, INT16)
        void set_ac_coolingTemp(const int16_t& coolingTemp,const std::string& mode,std::shared_ptr<Device>& device);
        
        // 处理空调命令
        void process_ac_wea1610_commands(const std::string& device_name);
        
        // 手动控制
        void ac_wea1610_manual_control(const std::string& device_name);
    
    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  
            
};


class KndIoMuduleCmd{
    public:
        KndIoMuduleCmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~KndIoMuduleCmd()=default;
        void io_module4040_operate(const std::string& switch_state,const int do_num,const std::string& mode,std::shared_ptr<Device>& device);
        void io_module4040_multi_operate(const std::vector<uint16_t>& switch_list,const std::string& mode,std::shared_ptr<Device>& device);

        void io_module2080_operate(const std::string& switch_state,const int do_num,const std::string& mode,std::shared_ptr<Device>& device);
        void io_module2080_multi_operate(const std::vector<uint16_t>& switch_list,const std::string& mode,std::shared_ptr<Device>& device);
        
        void process_iomodule4040_commands(const std::string& device_name);
        void iomodule4040_manual_control(const std::string& device_name);

        void process_iomodule2080_commands(const std::string& device_name);
        void iomodule2080_manual_control(const std::string& device_name);


    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  




};

class DehumifierV2Cmd {
    public:
        DehumifierV2Cmd(std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients,std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~DehumifierV2Cmd()=default;
        
        void set_humidity_action_value(const uint16_t& humidity_value,const std::string& mode,std::shared_ptr<Device>& device);
        void set_humidity_stop_value(const uint16_t& humidity_value,const std::string& mode,std::shared_ptr<Device>& device);
        void set_comm_address(const uint16_t& address,const std::string& mode,std::shared_ptr<Device>& device);
        void set_baud_rate(const uint16_t& baud_rate,const std::string& mode,std::shared_ptr<Device>& device);
        void set_manual_auto_mode(const uint16_t& mode_value,const std::string& mode,std::shared_ptr<Device>& device);
        
        void process_dehumifier_v2_commands(const std::string& device_name);
        void dehumifier_v2_manual_control(const std::string& device_name);
    
    private:
        std::unordered_map<int, std::shared_ptr<ModbusClient>> modbus_clients_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;  
};
    

class EmsCmd { 
    public:
        EmsCmd(std::unordered_map<std::string, std::shared_ptr<Device>> device_map);
        ~EmsCmd()=default;
        void manual_controll_do();
        void set_single_config(const std::string& cfg_name,bool& cfg_value); // 设置单个配置项的函数声明
        void set_single_config(const std::string& cfg_name,int& cfg_value); // 设置单个配置项的函数声明
        void set_single_config(const std::string& cfg_name,double& cfg_value); // 设置单个配置项的函数声明
        void set_multi_cfg(const json& cfg_name); // 设置多个配置项的函数声明
        void set_timingMode(const json& timingMode_cfg); // 设置定时模式配置的函数声明
        void set_demandResponseMode(const json& demandResponseMode_cfg);    // 设置需求响应模式配置的函数声明
        void process_ems_commands(EjPcs15AmCmd& pcs15am_cmd); // 处理EMS相关命令的函数声明
    private:
        std::shared_ptr<EMS> ems = nullptr;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;
        
};






#endif