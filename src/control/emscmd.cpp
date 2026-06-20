#include "command.h"
#include <iostream>
#include "qtcontroller.h"
#include "utils.h"
#include "log.h"

static auto& cmd = QtController::getInstance;  // 为单例获取函数创建别名

EmsCmd::EmsCmd(std::unordered_map<std::string, std::shared_ptr<Device>> device_map)
{
    if (!this->ems) {
        this->ems = EMS::instance();  // 初始化EMS实例
    }
    this->device_map_ = device_map;
}


void EmsCmd::manual_controll_do() {
    // (DO control removed: no GPIO hardware on this EMS)
}

void EmsCmd::set_single_config(const std::string& cfg_name,bool& cfg_value) {
    json config_data;
    config_data[cfg_name] = cfg_value;
    if (!this->ems->write_jsonfile(config_data)) {
        LOG_ERROR_LOC("Failed to write config for " + cfg_name);
    }else{
        // 使用线程安全的 setValue 方法
        this->ems->setValue<double>(cfg_name, cfg_value ? 1.0 : 0.0);
        // TODO 更新modbusTCP
    }
}


void EmsCmd::set_single_config(const std::string& cfg_name,int& cfg_value) {
    json config_data;
    config_data[cfg_name] = cfg_value;
    if (!this->ems->write_jsonfile(config_data)) {
        LOG_ERROR_LOC("Failed to write config for " + cfg_name);
    }else{
        // 使用线程安全的 setValue 方法
        this->ems->setValue<double>(cfg_name, static_cast<double>(cfg_value));
        // TODO 更新modbusTCP
    }
}

void EmsCmd::set_single_config(const std::string& cfg_name,double& cfg_value) {
    json config_data;
    config_data[cfg_name] = cfg_value;
    if (!this->ems->write_jsonfile(config_data)) {
        LOG_ERROR_LOC("Failed to write config for " + cfg_name);
    }else{
        // 使用线程安全的 setValue 方法
        this->ems->setValue<double>(cfg_name, cfg_value);
        // TODO 更新modbusTCP
    }
}


void EmsCmd::set_multi_cfg(const json& cfg_data) {
    if (!this->ems->write_jsonfile(cfg_data)) {
        LOG_ERROR_LOC("Failed to write multi config");
    }else{
        for (auto& item : cfg_data.items()) {
            const std::string& key = item.key();
            if (item.value().is_number_integer()) {
                // 使用线程安全的 setValue 方法
                this->ems->setValue<double>(key, static_cast<double>(item.value().get<int>()));
            }else if (item.value().is_number_float()) {
                // 使用线程安全的 setValue 方法
                this->ems->setValue<double>(key, item.value().get<double>());
            }
            // TODO 更新modbusTCP
        }
    }
}


void EmsCmd::set_timingMode(const json& timingMode_cfg) {
    json toSet_timingMode_cfg=  json::object();
    toSet_timingMode_cfg["timingModeSet"] = timingMode_cfg;

    if (!this->ems->write_timerJsonFile(toSet_timingMode_cfg)) {
        LOG_ERROR_LOC("Failed to write timing mode config");
    }else{
        this->ems->timingModeSet = timingMode_cfg;
        // TODO 更新modbusTCP
    }
}


void EmsCmd::set_demandResponseMode(const json& demandResponseMode_cfg) {
    json toSet_demandResponseMode_cfg=  json::object();
    toSet_demandResponseMode_cfg["demandResponseModeSet"] = demandResponseMode_cfg;

    if (!this->ems->write_timerJsonFile(toSet_demandResponseMode_cfg)) {
        LOG_ERROR_LOC("Failed to write demand response mode config");
    }else{
        this->ems->demandResponseModeSet = demandResponseMode_cfg;
        // TODO 更新modbusTCP
    }
}


void EmsCmd::process_ems_commands(EjPcsCmd& pcs_cmd,EjDcdcCmd& dcdc_cmd,GtbmsCmd& gtbms_cmd) { 
    try {
        auto& device_commands = cmd()->cmd_from_qt["ems"];

        manual_controll_do();   // do控制

        // 系统开机
        if (device_commands.contains("sys_startup") && device_commands["sys_startup"].is_number() && device_commands["sys_startup"].get<int>() == 1 ) {
            device_commands["sys_startup"] = false;
            // 使用线程安全的 setValue 方法
            this->ems->setValue<double>("开机", 1);
            LOG_INFO_LOC("系统开机指令已执行");
        }

        // 系统关机
        if (device_commands.contains("sys_shutdown") && device_commands["sys_shutdown"].is_number() && device_commands["sys_shutdown"].get<int>() == 1 ) {
            device_commands["sys_shutdown"] = false;
            // 使用线程安全的 setValue 方法
            this->ems->setValue<double>("开机", 0);
            if (this->device_map_["pcs1"])
                pcs_cmd.pcs_on_off("off","手动",this->device_map_["pcs1"]);
            if (this->device_map_["dcdc1"])
                dcdc_cmd.dcdc_on_off("off","手动",this->device_map_["dcdc1"]);
            if (this->device_map_["dcdc2"])
                dcdc_cmd.dcdc_on_off("off","手动",this->device_map_["dcdc2"]);
            if (this->device_map_["gtbms485"])
                gtbms_cmd.gtbms_vol_on_off("off","手动",this->device_map_["gtbms485"]);
            
            LOG_INFO_LOC("系统关机指令已执行");
        }

        // 设置SOC保护和回差
        if (device_commands.contains("sys_setSocProtect") && device_commands["sys_setSocProtect"].is_boolean() && device_commands["sys_setSocProtect"].get<bool>()) {
            device_commands["sys_setSocProtect"] = false;
            json soc_protect_cfg;
            
            if (device_commands.contains("sys_setStopChargeSoc") && device_commands["sys_setStopChargeSoc"].is_number()) {
                soc_protect_cfg["停止充电SOC"] = device_commands["sys_setStopChargeSoc"].get<int>();
            }
            if (device_commands.contains("sys_setRdStopChargeSoc") && device_commands["sys_setRdStopChargeSoc"].is_number()) {
                soc_protect_cfg["停止充电SOC回差"] = device_commands["sys_setRdStopChargeSoc"].get<int>();
            }
            if (device_commands.contains("sys_setEndDischargeSoc") && device_commands["sys_setEndDischargeSoc"].is_number()) {
                soc_protect_cfg["停止放电SOC"] = device_commands["sys_setEndDischargeSoc"].get<int>();
            }
            if (device_commands.contains("sys_setRdEndDischargeSoc") && device_commands["sys_setRdEndDischargeSoc"].is_number()) {
                soc_protect_cfg["停止放电SOC回差"] = device_commands["sys_setRdEndDischargeSoc"].get<int>();
            }
            
            set_multi_cfg(soc_protect_cfg);
            LOG_INFO_LOC("SOC保护配置已更新");
        }

        // 使能SOC保护
        if (device_commands.contains("sys_setEnableSocProtect") && !device_commands["sys_setEnableSocProtect"].is_null()) {
            bool enable_soc_protect = device_commands["sys_setEnableSocProtect"].get<int>() != 0;
            device_commands["sys_setEnableSocProtect"] = nullptr;
            set_single_config("使能SOC保护", enable_soc_protect);
            LOG_INFO_LOC("SOC保护使能设置为: " + std::to_string(enable_soc_protect));
        }

        // 使能单体电压保护
        if (device_commands.contains("sys_setEnableCellVolProtect") && !device_commands["sys_setEnableCellVolProtect"].is_null()) {
            bool enable_cell_vol_protect = device_commands["sys_setEnableCellVolProtect"].get<int>() != 0;
            device_commands["sys_setEnableCellVolProtect"] = nullptr;
            set_single_config("使能单体电压保护", enable_cell_vol_protect);
            LOG_INFO_LOC("单体电压保护使能设置为: " + std::to_string(enable_cell_vol_protect));
        }

        // 设置电网输入配置参数
        if (device_commands.contains("sys_setInput") && device_commands["sys_setInput"].is_boolean() && device_commands["sys_setInput"].get<bool>()) {
            device_commands["sys_setInput"] = false;
            json input_cfg;
            
            if (device_commands.contains("sys_setInputRatedPower") && device_commands["sys_setInputRatedPower"].is_number()) {
                input_cfg["电网输入额定功率"] = device_commands["sys_setInputRatedPower"].get<int>();
            }
            if (device_commands.contains("sys_setInputMaxPower") && device_commands["sys_setInputMaxPower"].is_number()) {
                input_cfg["电网输入最大功率"] = device_commands["sys_setInputMaxPower"].get<int>();
            }
            
            set_multi_cfg(input_cfg);
            LOG_INFO_LOC("电网输入配置已更新");
        }

        // 设置BMS配置参数
        if (device_commands.contains("sys_setBms") && device_commands["sys_setBms"].is_boolean() && device_commands["sys_setBms"].get<bool>()) {
            device_commands["sys_setBms"] = false;
            json bms_cfg;
            
            if (device_commands.contains("sys_setEndChargeCellVoltage") && device_commands["sys_setEndChargeCellVoltage"].is_number()) {
                bms_cfg["停止充电单体电压"] = device_commands["sys_setEndChargeCellVoltage"].get<double>();
            }
            if (device_commands.contains("sys_setRdEndChargeCellVoltage") && device_commands["sys_setRdEndChargeCellVoltage"].is_number()) {
                bms_cfg["停止充电单体电压回差"] = device_commands["sys_setRdEndChargeCellVoltage"].get<double>();
            }
            if (device_commands.contains("sys_setEndDischargeCellVoltage") && device_commands["sys_setEndDischargeCellVoltage"].is_number()) {
                bms_cfg["停止放电单体电压"] = device_commands["sys_setEndDischargeCellVoltage"].get<double>();
            }
            if (device_commands.contains("sys_setRdEndDischargeCellVoltage") && device_commands["sys_setRdEndDischargeCellVoltage"].is_number()) {
                bms_cfg["停止放电单体电压回差"] = device_commands["sys_setRdEndDischargeCellVoltage"].get<double>();
            }
            
            set_multi_cfg(bms_cfg);
            LOG_INFO_LOC("BMS配置已更新");
        }

        // 设置PCS配置参数
        if (device_commands.contains("sys_setPcs") && device_commands["sys_setPcs"].is_boolean() && device_commands["sys_setPcs"].get<bool>() ) {
            device_commands["sys_setPcs"] = false;
            json pcs_cfg;
            
            if (device_commands.contains("sys_setPcsRatedPower") && device_commands["sys_setPcsRatedPower"].is_number()) {
                pcs_cfg["PCS额定功率"] = device_commands["sys_setPcsRatedPower"].get<int>();
            }
            if (device_commands.contains("sys_setPcsMaxChargePower") && device_commands["sys_setPcsMaxChargePower"].is_number()) {
                pcs_cfg["PCS最大充电功率"] = device_commands["sys_setPcsMaxChargePower"].get<int>();
            }
            if (device_commands.contains("sys_setPcsMaxDischargePower") && device_commands["sys_setPcsMaxDischargePower"].is_number()) {
                pcs_cfg["PCS最大放电功率"] = device_commands["sys_setPcsMaxDischargePower"].get<int>();
            }
            
            set_multi_cfg(pcs_cfg);
            LOG_INFO_LOC("PCS配置已更新");
        }

        // 设置定时模式
        if (device_commands.contains("sys_setTimer") && device_commands["sys_setTimer"].is_number() && device_commands["sys_setTimer"].get<int>()!=0) {
            device_commands["sys_setTimer"] = false;
            if (device_commands.contains("timingModeSet") && !device_commands["timingModeSet"].is_null()) {
                const auto& timing_data = device_commands["timingModeSet"];
                
                // 验证数据类型：应该是对象（包含chargeTimeList和dischargeTimeList）
                if (!timing_data.is_object()) {
                    LOG_WARNING_LOC("定时模式数据格式错误，期望对象");
                } else {
                    set_timingMode(timing_data);
                    LOG_INFO_LOC("定时模式配置已更新");
                }
            } else {
                LOG_WARNING_LOC("定时模式设置触发，但未找到timingModeSet数据");
            }
        }

        // 设置需求响应模式
        if (device_commands.contains("sys_setDemandResponse") && device_commands["sys_setDemandResponse"].is_number() && device_commands["sys_setDemandResponse"].get<int>()!=0) {
            device_commands["sys_setDemandResponse"] = false;
            if (device_commands.contains("demandResponseModeSet") && !device_commands["demandResponseModeSet"].is_null()) {
                const auto& demand_response_data = device_commands["demandResponseModeSet"];
                
                // 验证数据类型：应该是数组或对象
                if (!demand_response_data.is_array() && !demand_response_data.is_object()) {
                    LOG_WARNING_LOC("需求响应模式数据格式错误，期望数组或对象");
                } else {
                    set_demandResponseMode(demand_response_data);
                    LOG_INFO_LOC("需求响应模式配置已更新");
                }
            } else {
                LOG_WARNING_LOC("需求响应模式设置触发，但未找到demandResponseModeSet数据");
            }
        }

        // 系统运行模式切换
        if (device_commands.contains("sys_setMode") && device_commands["sys_setMode"].get<int>() != 0 && device_commands["sys_setMode"].is_number()) {
            int mode = device_commands["sys_setMode"].get<int>();
            device_commands["sys_setMode"] = 0;
            // 使用线程安全的 setValue 方法
            this->ems->setValue<double>("系统运行模式", static_cast<double>(mode));
            LOG_INFO_LOC("系统运行模式设置为: " + std::to_string(mode));
        }

        // 系统故障复归
        if (device_commands.contains("sys_reset") && device_commands["sys_reset"].is_number() && device_commands["sys_reset"].get<int>() != 0) {
            device_commands["sys_reset"] = 0;
            if (this->device_map_["pcs1"])
                pcs_cmd.pcs_reset(this->device_map_["pcs1"],"定时");
            this->ems->setValue<double>("系统状态", 2);
            LOG_INFO_LOC("系统故障复位指令已执行");

        }

        // 设置自动模式充电功率
        if (device_commands.contains("sys_setAutoModeChargePower") && !device_commands["sys_setAutoModeChargePower"].is_null()) {
            int charge_power = device_commands["sys_setAutoModeChargePower"].get<int>();
            device_commands["sys_setAutoModeChargePower"] = nullptr;
            set_single_config("自动模式充电功率", charge_power);
            LOG_INFO_LOC("自动模式充电功率设置为: " + std::to_string(charge_power));
        }

        // 设置系统并离网
        if (device_commands.contains("sys_setGridMode") && !device_commands["sys_setGridMode"].is_null()) {
            int grid_mode = device_commands["sys_setGridMode"].get<int>();
            device_commands["sys_setGridMode"] = nullptr;
            // 使用线程安全的 setValue 方法
            this->ems->setValue<double>("系统并离网", static_cast<double>(grid_mode));
            LOG_INFO_LOC("系统并离网模式设置为: " + std::to_string(grid_mode));
        }

        // 设置液冷机开机电芯温度
        if (device_commands.contains("set_coolerStartBatTemp") && !device_commands["set_coolerStartBatTemp"].is_null()) {
            int temp = device_commands["set_coolerStartBatTemp"].get<int>();
            device_commands["set_coolerStartBatTemp"] = nullptr;
            set_single_config("coolerStartBatTemp", temp);
            LOG_INFO_LOC("液冷机开机电芯温度设置为: " + std::to_string(temp));
        }

        // 设置液冷机关机电芯温度
        if (device_commands.contains("set_coolerStopBatTemp") && !device_commands["set_coolerStopBatTemp"].is_null()) {
            int temp = device_commands["set_coolerStopBatTemp"].get<int>();
            device_commands["set_coolerStopBatTemp"] = nullptr;
            set_single_config("coolerStopBatTemp", temp);
            LOG_INFO_LOC("液冷机关机电芯温度设置为: " + std::to_string(temp));
        }

        // 设置循环水泵开机电芯温度
        if (device_commands.contains("set_pumpStartBatTemp") && !device_commands["set_pumpStartBatTemp"].is_null()) {
            int temp = device_commands["set_pumpStartBatTemp"].get<int>();
            device_commands["set_pumpStartBatTemp"] = nullptr;
            set_single_config("pumpStartBatTemp", temp);
            LOG_INFO_LOC("循环水泵开机电芯温度设置为: " + std::to_string(temp));
        }

        // 设置循环水泵关机电芯温度
        if (device_commands.contains("set_pumpStopBatTemp") && !device_commands["set_pumpStopBatTemp"].is_null()) {
            int temp = device_commands["set_pumpStopBatTemp"].get<int>();
            device_commands["set_pumpStopBatTemp"] = nullptr;
            set_single_config("pumpStopBatTemp", temp);
            LOG_INFO_LOC("循环水泵关机电芯温度设置为: " + std::to_string(temp));
        }

        // 重过载治理功率
        if (device_commands.contains("set_olRegulationPower") && !device_commands["set_olRegulationPower"].is_null()) {
            int power = device_commands["set_olRegulationPower"].get<int>();
            device_commands["set_olRegulationPower"] = nullptr;
            set_single_config("重过载治理功率", power);
            LOG_INFO_LOC("重过载治理功率设置为: " + std::to_string(power));
        }

        // 重过载确认时间
        if (device_commands.contains("set_olDetectTime") && !device_commands["set_olDetectTime"].is_null()) {
            int time_val = device_commands["set_olDetectTime"].get<int>();
            device_commands["set_olDetectTime"] = nullptr;
            set_single_config("重过载确认时间", time_val);
            LOG_INFO_LOC("重过载确认时间设置为: " + std::to_string(time_val));
        }

        // 重过载调整间隔
        if (device_commands.contains("set_olStepTime") && !device_commands["set_olStepTime"].is_null()) {
            int step_time = device_commands["set_olStepTime"].get<int>();
            device_commands["set_olStepTime"] = nullptr;
            set_single_config("重过载调整间隔", step_time);
            LOG_INFO_LOC("重过载调整间隔设置为: " + std::to_string(step_time));
        }

        // 重过载步长
        if (device_commands.contains("set_olStepPower") && !device_commands["set_olStepPower"].is_null()) {
            int step_power = device_commands["set_olStepPower"].get<int>();
            device_commands["set_olStepPower"] = nullptr;
            set_single_config("重过载步长", step_power);
            LOG_INFO_LOC("重过载步长设置为: " + std::to_string(step_power));
        }

        // 重过载最大放电功率
        if (device_commands.contains("set_olMaxDischargePower") && !device_commands["set_olMaxDischargePower"].is_null()) {
            int max_power = device_commands["set_olMaxDischargePower"].get<int>();
            device_commands["set_olMaxDischargePower"] = nullptr;
            set_single_config("重过载最大放电功率", max_power);
            LOG_INFO_LOC("重过载最大放电功率设置为: " + std::to_string(max_power));
        }

        // 重过载治理降点功率
        if (device_commands.contains("set_olReductionrPoint") && !device_commands["set_olReductionrPoint"].is_null()) {
            int reduction_point = device_commands["set_olReductionrPoint"].get<int>();
            device_commands["set_olReductionrPoint"] = nullptr;
            set_single_config("重过载治理降点功率", reduction_point);
            LOG_INFO_LOC("重过载治理降点功率设置为: " + std::to_string(reduction_point));
        }

        // 补点允许功率
        if (device_commands.contains("set_olCompensationAllowPoint") && !device_commands["set_olCompensationAllowPoint"].is_null()) {
            int allow_point = device_commands["set_olCompensationAllowPoint"].get<int>();
            device_commands["set_olCompensationAllowPoint"] = nullptr;
            set_single_config("补点允许功率", allow_point);
            LOG_INFO_LOC("补点允许功率设置为: " + std::to_string(allow_point));
        }

        // 重载补点步长
        if (device_commands.contains("set_olCompensationStepPower") && !device_commands["set_olCompensationStepPower"].is_null()) {
            int comp_step = device_commands["set_olCompensationStepPower"].get<int>();
            device_commands["set_olCompensationStepPower"] = nullptr;
            set_single_config("重载补点步长", comp_step);
            LOG_INFO_LOC("重载补点步长设置为: " + std::to_string(comp_step));
        }

        // 补点降低点功率
        if (device_commands.contains("set_olCompensationReductionPoint") && !device_commands["set_olCompensationReductionPoint"].is_null()) {
            int comp_reduction = device_commands["set_olCompensationReductionPoint"].get<int>();
            device_commands["set_olCompensationReductionPoint"] = nullptr;
            set_single_config("补点降低点功率", comp_reduction);
            LOG_INFO_LOC("补点降低点功率设置为: " + std::to_string(comp_reduction));
        }

        // 补点最大充电功率
        if (device_commands.contains("set_olCompensationMaxChargePower") && !device_commands["set_olCompensationMaxChargePower"].is_null()) {
            int max_charge = device_commands["set_olCompensationMaxChargePower"].get<int>();
            device_commands["set_olCompensationMaxChargePower"] = nullptr;
            set_single_config("补点最大充电功率", max_charge);
            LOG_INFO_LOC("补点最大充电功率设置为: " + std::to_string(max_charge));
        }

        // 低电压确认阈值
        if (device_commands.contains("set_lvRegulationVol") && !device_commands["set_lvRegulationVol"].is_null()) {
            int vol = device_commands["set_lvRegulationVol"].get<int>();
            device_commands["set_lvRegulationVol"] = nullptr;
            set_single_config("低电压确认阈值", vol);
            LOG_INFO_LOC("低电压确认阈值设置为: " + std::to_string(vol));
        }

        // 低电压确认时间
        if (device_commands.contains("set_lvDetectTime") && !device_commands["set_lvDetectTime"].is_null()) {
            int detect_time = device_commands["set_lvDetectTime"].get<int>();
            device_commands["set_lvDetectTime"] = nullptr;
            set_single_config("低电压确认时间", detect_time);
            LOG_INFO_LOC("低电压确认时间设置为: " + std::to_string(detect_time));
        }

        // 低压治理调整间隔
        if (device_commands.contains("set_lvStepTime") && !device_commands["set_lvStepTime"].is_null()) {
            int step_time = device_commands["set_lvStepTime"].get<int>();
            device_commands["set_lvStepTime"] = nullptr;
            set_single_config("低压治理调整间隔", step_time);
            LOG_INFO_LOC("低压治理调整间隔设置为: " + std::to_string(step_time));
        }

        // 无功调节步长
        if (device_commands.contains("set_lvReactiveStep") && !device_commands["set_lvReactiveStep"].is_null()) {
            int reactive_step = device_commands["set_lvReactiveStep"].get<int>();
            device_commands["set_lvReactiveStep"] = nullptr;
            set_single_config("无功调节步长", reactive_step);
            LOG_INFO_LOC("无功调节步长设置为: " + std::to_string(reactive_step));
        }

        // 低压治理最大无功功率
        if (device_commands.contains("set_lvMaxReactivePower") && !device_commands["set_lvMaxReactivePower"].is_null()) {
            int max_reactive = device_commands["set_lvMaxReactivePower"].get<int>();
            device_commands["set_lvMaxReactivePower"] = nullptr;
            set_single_config("低压治理最大无功功率", max_reactive);
            LOG_INFO_LOC("低压治理最大无功功率设置为: " + std::to_string(max_reactive));
        }

        // 有功调节步长
        if (device_commands.contains("set_lvActiveStep") && !device_commands["set_lvActiveStep"].is_null()) {
            int active_step = device_commands["set_lvActiveStep"].get<int>();
            device_commands["set_lvActiveStep"] = nullptr;
            set_single_config("有功调节步长", active_step);
            LOG_INFO_LOC("有功调节步长设置为: " + std::to_string(active_step));
        }

        // 低压治理最大有功功率
        if (device_commands.contains("set_lvMaxActivePower") && !device_commands["set_lvMaxActivePower"].is_null()) {
            int max_active = device_commands["set_lvMaxActivePower"].get<int>();
            device_commands["set_lvMaxActivePower"] = nullptr;
            set_single_config("低压治理最大有功功率", max_active);
            LOG_INFO_LOC("低压治理最大有功功率设置为: " + std::to_string(max_active));
        }

        // 低压治理目标电压阈值
        if (device_commands.contains("set_lvTargetVol") && !device_commands["set_lvTargetVol"].is_null()) {
            int target_vol = device_commands["set_lvTargetVol"].get<int>();
            device_commands["set_lvTargetVol"] = nullptr;
            set_single_config("低压治理目标电压阈值", target_vol);
            LOG_INFO_LOC("低压治理目标电压阈值设置为: " + std::to_string(target_vol));
        }

        // 高电压动作值
        if (device_commands.contains("set_lvComposationStartVol") && !device_commands["set_lvComposationStartVol"].is_null()) {
            int start_vol = device_commands["set_lvComposationStartVol"].get<int>();
            device_commands["set_lvComposationStartVol"] = nullptr;
            set_single_config("高电压动作值", start_vol);
            LOG_INFO_LOC("高电压动作值设置为: " + std::to_string(start_vol));
        }

        // 动作调整步长
        if (device_commands.contains("set_lvStepPower") && !device_commands["set_lvStepPower"].is_null()) {
            int step_power = device_commands["set_lvStepPower"].get<int>();
            device_commands["set_lvStepPower"] = nullptr;
            set_single_config("动作调整步长", step_power);
            LOG_INFO_LOC("动作调整步长设置为: " + std::to_string(step_power));
        }

        // 高电压最大充电功率
        if (device_commands.contains("set_lvMaxChargePower") && !device_commands["set_lvMaxChargePower"].is_null()) {
            int max_charge = device_commands["set_lvMaxChargePower"].get<int>();
            device_commands["set_lvMaxChargePower"] = nullptr;
            set_single_config("高电压最大充电功率", max_charge);
            LOG_INFO_LOC("高电压最大充电功率设置为: " + std::to_string(max_charge));
        }

        // 高电压恢复电压
        if (device_commands.contains("set_lvMinComposationVol") && !device_commands["set_lvMinComposationVol"].is_null()) {
            int min_vol = device_commands["set_lvMinComposationVol"].get<int>();
            device_commands["set_lvMinComposationVol"] = nullptr;
            set_single_config("高电压恢复电压", min_vol);
            LOG_INFO_LOC("高电压恢复电压设置为: " + std::to_string(min_vol));
        }

        // 防逆流死区
        if (device_commands.contains("set_antiRefluxDeadZone") && !device_commands["set_antiRefluxDeadZone"].is_null()) {
            int dead_zone = device_commands["set_antiRefluxDeadZone"].get<int>();
            device_commands["set_antiRefluxDeadZone"] = nullptr;
            set_single_config("防逆流死区", dead_zone);
            LOG_INFO_LOC("防逆流死区设置为: " + std::to_string(dead_zone));
        }

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error processing EMS commands: " + std::string(e.what()));
    }
}
