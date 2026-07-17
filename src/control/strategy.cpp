#include "strategy.h"
#include <thread>
#include <chrono>
#include "devicemanager.h"
#include "log.h"



Strategy::Strategy(std::shared_ptr<DeviceManager> device_manager)
    : device_manager_(device_manager)
{
    // 设备命令对象初始化（与 DeviceManager 风格一致，注释掉即可禁用对应设备）
    this->ems_cmd_      = std::make_shared<EmsCmd>(device_manager_->device_map_);
    this->pcs15am_cmd_ = std::make_shared<EjPcs15AmCmd>(device_manager_->getModbusClients(), device_manager_->device_map_);
    this->gtbms_cmd_    = std::make_shared<GtbmsCmd>(device_manager_->getModbusClients(), device_manager_->device_map_);

}

Strategy::~Strategy() {
    stopThread();   // 确保线程在对象销毁前停止
}

void Strategy::stopThread() {
    keep_running_ = false;          // 通知线程退出
    if (worker_thread_.joinable()) {
        worker_thread_.join();      // 等待线程真正结束
    }
    if (pilot_lamp_show_thread_.joinable()) {
        pilot_lamp_show_thread_.join();      // 等待线程真正结束
    }
}

void Strategy::runningThread() {
    // 如果已有线程在运行，先停止
    if (worker_thread_.joinable()) {
        stopThread();
    }
    if (pilot_lamp_show_thread_.joinable()) {
        stopThread();
    }
    this->worker_thread_ = std::thread([this]() { sysRun(); });
    // ✨ 不再 detach，保持 joinable
    this->pilot_lamp_show_thread_ = std::thread([this]() { pilotLampShowThread(); });
    
}

void Strategy::sysRun() {
    while (keep_running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto ems = EMS::instance();
        ems->heartbeat = static_cast<int>(std::time(nullptr));

        // 使用线程安全的 getValue 方法
        int current_mode = static_cast<int>(ems->getValue<double>("系统运行模式", 1));
        int startup = static_cast<int>(ems->getValue<double>("开机", 0));

        if (!(startup == 1)) {
            // 关机状态：清理模式状态
            if (last_mode_ != -1) {
                exitMode(last_mode_);
                last_mode_ = -1;
            }
            manualModeRun();
            // 轮询告警
            uint16_t alarm_level = pollingAlarm();
            if (alarm_level == 3) {
                alarmLv3Protect();
            }
        } else {
            // 模式切换检测
            if (current_mode != last_mode_) {
                exitMode(last_mode_);
                enterMode(current_mode);
                last_mode_ = current_mode;
            }

            // 根据当前模式执行相应的逻辑
            switch (current_mode) {
                case RunMode::MANUAL:  // 手动模式
                    manualModeRun();
                    break;
                case RunMode::AUTO:  // 自动模式
                    autoModeRun();
                    break;
                case RunMode::TIMER:  // 定时/定时模式
                    weeklyPlanModeRun();
                    break;
                case RunMode::DEMAND_RESPONSE:  // 需求响应模式
                    demandResponseModeRun();
                    break;
                default:
                    LOG_WARNING_LOC("未定义运行模式: " + std::to_string(current_mode));
                    manualModeRun();
                    break;
            }
        }
    }
}


// 定时模式启动过程
bool Strategy::timerStartupProcess() {
    auto ems = EMS::instance();
    auto pcs1_device = device_manager_->getDeviceByName("pcs1");
    
    if (!pcs1_device) {
        return false;
    }
    
    ems->sys_running_pos.store(13);
    
    // TODO: BMS上高压
    // gtbms_cmd.gtbms_vol_on_off(switch='on', dev_name='gtbms485', mode='定时启动')
    
    // (DO1 control removed: no GPIO hardware on this EMS)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 检查PCS状态
    if (pcs1_device->online_status) {
        // 使用线程安全的 getValue 方法
        double offgrid_mode = pcs1_device->getValue<double>("离网模式设置", -1);
        double work_state = pcs1_device->getValue<double>("工作状态", -1);
        
        if (offgrid_mode == 0 && work_state != 64) {
            LOG_INFO_LOC("定时模式开机检测成功");
            return true;
        }
    }
    
    // 等待PCS上线
    int count = 0;
    while (count < 32) {
        count++;
        manualModeRun();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (pcs1_device->online_status) {
            break;
        }
    }
    
    // TODO: PCS配置和复位
    
    if (pcs1_device->online_status) {
        double work_state = pcs1_device->getValue<double>("工作状态", -1);
        if (work_state != 64) {
            LOG_INFO_LOC("定时模式开机检测成功");
            return true;
        }
    }
    
    LOG_INFO_LOC("定时模式开机检测失败，PCS故障或不在线");
    return false;
}

// 需求响应模式启动过程
bool Strategy::demandResponseStartupProcess() {
    auto ems = EMS::instance();
    auto pcs1_device = device_manager_->getDeviceByName("pcs1");
    
    if (!pcs1_device) {
        return false;
    }
    
    // TODO: BMS上高压
    // gtbms_cmd.gtbms_vol_on_off(switch='on', dev_name='gtbms485', mode='定时启动')
    
    // (DO1 control removed: no GPIO hardware on this EMS)
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 检查PCS状态
    if (pcs1_device->online_status) {
        double offgrid_mode = pcs1_device->getValue<double>("离网模式设置", -1);
        double work_state = pcs1_device->getValue<double>("工作状态", -1);
        
        if (offgrid_mode == 0 && work_state != 64) {
            LOG_INFO_LOC("需求响应模式开机检测成功");
            return true;
        }
    }
    
    // 等待PCS上线
    int count = 0;
    while (count < 32) {
        count++;
        manualModeRun();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        if (pcs1_device->online_status) {
            break;
        }
    }
    
    // TODO: PCS配置和复位
    
    if (pcs1_device->online_status) {
        double work_state = pcs1_device->getValue<double>("工作状态", -1);
        if (work_state != 64) {
            LOG_INFO_LOC("需求响应模式开机检测成功");
            return true;
        }
    }
    
    LOG_INFO_LOC("需求响应模式开机检测失败，PCS故障或不在线");
    return false;
}


void Strategy::autoModeRun(){
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    //TODO: 自动模式的运行逻辑

}

void Strategy::manualModeRun() {
    // 处理 EMS 命令
    if (pcs15am_cmd_ && gtbms_cmd_) {
        ems_cmd_->process_ems_commands(*pcs15am_cmd_, *gtbms_cmd_);
    } 
    // 手动模式的运行逻辑
    pcs15am_cmd_->process_pcs_commands("pcs1");

    // dcdc_cmd_->process_dcdc_commands("dcdc1");
    // dcdc_cmd_->process_dcdc_commands("dcdc2");

    gtbms_cmd_->process_gtbms_commands("gtbms485");

    // hengdu_ac_cmd_->process_hengdu_ac_commands("air_condition");

    // dg_hgm6100_cmd_->process_dg_hgm6100n_commands("dg_hgm6100n");

}


void Strategy::weeklyPlanModeRun() {
    try {
        auto ems = EMS::instance();
        auto bms_device = device_manager_->getDeviceByName("bms_uhome");
        auto pcs1_device = device_manager_->getDeviceByName("pcs1");
        auto acmeter_device = device_manager_->getDeviceByName("dtsd3366");

        if (!pcs1_device || !acmeter_device) {
            return;
        }

        ems->sys_running_pos.store(15);

        manualModeRun();

    //     if (ems->getValue<double>("开机", 0) <1.0) break;

    //     // 检查模式是否改变
    //     int current_mode = static_cast<int>(ems->getValue<double>("系统运行模式", 1));
    //     if (current_mode != 3) {
    //         LOG_INFO_LOC("周定时模式退出, 准备切换至模式: " + std::to_string(current_mode));
    //         break;
    //     }

    //     // 热管理和水泵管理
    //     // thermalManager();
    //     // waterPumpManager();

    //     // 轮询告警
    //     uint16_t alarm_level = pollingAlarm();

    //     if (alarm_level == 0 || alarm_level == 1) {
    //         ems->sys_running_pos.store(16);
    //         timer_fault_occurred_ = false;

    //         // 检查充放电状态
    //         auto [should_run_week_plan, week_plan_power_need] = ems->check_charge_status();
    //         ems->shouldRunWeekPlan = should_run_week_plan;

    //         if (should_run_week_plan) {
    //             if (week_plan_power_need < 0) {
    //                 // 充电模式
    //                 ems->sys_running_pos.store(30);
    //                 ems->weekPlanPower_need = ems->get_max_charge_power(week_plan_power_need);
    //                 double pcs_power = pcs1_device->getValue<double>("有功功率设置", 0);
    //                 // 检查保护条件 - 使用线程安全的 getValue
    //                 if ((ems->getValue<double>("使能单体电压保护", 0) > 0.5 &&
    //                      ems->reach_setting_upper_cell_voltage(bms_device)) ||
    //                     (ems->getValue<double>("使能SOC保护", 0) > 0.5 &&
    //                      ems->check_soc_is_end_charge(bms_device)) ||
    //                     ems->fully_charged_confirm(bms_device)) {
    //                     timer_allow_charge_ = false;
    //                 }

    //                 if (!timer_allow_charge_) {
    //                     ems->sys_running_pos.store(31);

    //                     // 如果正在充电，停止充电 - 使用线程安全的 getValue
    //                     if (pcs_power < 0 || ems->weekPlanPower_need < 0) {
    //                         if (static_cast<int>(last_sent_pcs_power_) != 0) {
    //                             pcs15am_cmd_->pcs_set_power(0, "定时", pcs1_device);
    //                             last_sent_pcs_power_ = 0;
    //                         }
    //                     }

    //                     // 检查是否回落到回差范围
    //                     if (ems->check_charge_rd_recover(bms_device)) {
    //                         timer_allow_charge_ = true;
    //                     }
    //                 } else {
    //                     ems->sys_running_pos.store(32);
    //                     if (static_cast<int>(last_sent_pcs_power_) != static_cast<int>(ems->weekPlanPower_need)) {
    //                         // 设置充电功率
    //                         pcs15am_cmd_->pcs_set_power(ems->weekPlanPower_need, "定时", pcs1_device);
    //                         last_sent_pcs_power_ = ems->weekPlanPower_need;
    //                     }

    //                     // 开启PCS - 使用线程安全的 getValue
    //                     double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
    //                     if (pcs_switch != 1) {
    //                         pcs15am_cmd_->pcs_on_off("on", "定时", pcs1_device);
    //                     }

    //                 }
    //             } else {
    //                     // 放电模式
    //                     ems->sys_running_pos.store(33);
    //                     ems->weekPlanPower_need = ems->get_max_discharge_power(week_plan_power_need);
    //                     double pcs_power = pcs1_device->getValue<double>("有功功率设置", 0);

    //                     // 检查保护条件 - 使用线程安全的 getValue
    //                     if ((ems->getValue<double>("使能单体电压保护", 0) > 0.5 &&
    //                         ems->fall_to_setting_lower_cell_voltage(bms_device)) ||
    //                         (ems->getValue<double>("使能SOC保护", 0) > 0.5 &&
    //                         ems->check_soc_is_end_discharge(bms_device)) ||
    //                         ems->fully_discharged_confirm(bms_device)) {
    //                         timer_allow_discharge_ = false;
    //                     }

    //                     if (!timer_allow_discharge_) {
    //                         // 如果正在放电，停止放电

    //                         if (pcs_power > 0 || ems->weekPlanPower_need > 0) {
    //                             if (static_cast<int>(last_sent_pcs_power_) != 0) {
    //                                 pcs15am_cmd_->pcs_set_power(0, "定时", pcs1_device);
    //                                 last_sent_pcs_power_ = 0;
    //                             }
    //                         }

    //                         // 检查是否回落到回差范围
    //                         if (ems->check_discharge_rd_recover(bms_device)) {
    //                             timer_allow_discharge_ = true;
    //                         }
    //                     } else {
    //                         if (static_cast<int>(last_sent_pcs_power_) != static_cast<int>(ems->weekPlanPower_need)) {
    //                             // 设置放电功率
    //                             pcs15am_cmd_->pcs_set_power(ems->weekPlanPower_need,
    //                                             "定时", pcs1_device);
    //                             last_sent_pcs_power_ = ems->weekPlanPower_need;
    //                         }

    //                         // 开启PCS
    //                         double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
    //                         if (pcs_switch != 1) {
    //                             pcs15am_cmd_->pcs_on_off("on", "定时", pcs1_device);
    //                         }
    //                     }

    //                 }

    //         } else {
    //             // 不在计划时段，关闭PCS
    //             ems->sys_running_pos.store(34);

    //             double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
    //             if (pcs_switch != 0) {
    //                 pcs15am_cmd_->pcs_on_off("off", "定时", pcs1_device);
    //             }

    //             // 重置上次发送的功率值
    //             if (static_cast<int>(last_sent_pcs_power_) != 0) {
    //                 last_sent_pcs_power_ = 0;
    //             }
    //         }
    //     } else if (alarm_level == 2) {
    //         // 二级告警
    //         ems->sys_running_pos.store(17);
    //         alarmLv2Protect();

    //         if (!timer_fault_occurred_) {
    //             timer_fault_happen_time_ = std::chrono::steady_clock::now();
    //             timer_fault_occurred_ = true;
    //         }

    //         auto current_time = std::chrono::steady_clock::now();
    //         auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
    //             current_time - timer_fault_happen_time_).count();

    //         // 180秒后自动恢复
    //         if (elapsed > 180) {
    //             pcs15am_cmd_->pcs_reset(pcs1_device, "定时");

    //             // 使用线程安全的 setValue 方法
    //             ems->setValue<double>("系统状态", 2);
    //             ems->setValue<double>("系统告警等级", 0);
    //             timer_fault_occurred_ = false;

    //             LOG_INFO_LOC("定时系统二级告警自恢复完成");
    //             std::this_thread::sleep_for(std::chrono::seconds(1));
    //         }
    //     } else {
    //         // 三级告警
    //         ems->sys_running_pos.store(18);
    //         alarmLv3Protect();

    //         // 使用线程安全的 setValue 方法
    //         ems->setValue<double>("开机", 0);
    //         ems->setValue<double>("系统运行模式", 1);  // 返回手动模式

    //         LOG_INFO_LOC("三级告警，退出定时模式");
    //     }

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("定时模式运行异常: " + std::string(e.what()));
    }
}


void Strategy::demandResponseModeRun() {
    try {
        auto ems = EMS::instance();
        auto bms_device = device_manager_->getDeviceByName("gtbms485");
        auto pcs1_device = device_manager_->getDeviceByName("pcs1");

        if (!pcs1_device || !ems) {
            return;
        }

        ems->sys_running_pos.store(19);

        // 处理手动命令
        manualModeRun();

        // if (ems->getValue<double>("开机", 0) <1.0) break;

        // // 检查模式是否改变 - 使用线程安全的 getValue
        // int current_mode = static_cast<int>(ems->getValue<double>("系统运行模式", 1));
        // if (current_mode != 4) {
        //     LOG_INFO_LOC("Demand response mode exited, current mode: " + std::to_string(current_mode));
        //     break;
        // }

        // // 热管理和水泵管理
        // // thermalManager();
        // // waterPumpManager();

        // // 轮询告警
        // uint16_t alarm_level = pollingAlarm();

        // if (alarm_level == 0 || alarm_level == 1) {
        //     ems->sys_running_pos.store(20);
        //     demand_fault_occurred_ = false;

        //     // 检查需求响应状态
        //     auto [should_run_demand_response, demand_power_need, reactive_power] =
        //         ems->check_demand_response_status();
        //     ems->shouldRunDemandResponse = should_run_demand_response;

        //     if (should_run_demand_response) {
        //         if (demand_power_need < 0) {
        //             // 充电模式
        //             ems->sys_running_pos.store(40);
        //             ems->demandPower_need = ems->get_max_charge_power(demand_power_need);

        //             // 检查保护条件 - 使用线程安全的 getValue
        //             if ((ems->getValue<double>("使能单体电压保护", 0) > 0.5 &&
        //                  ems->reach_setting_upper_cell_voltage(bms_device)) ||
        //                 (ems->getValue<double>("使能SOC保护", 0) > 0.5 &&
        //                  ems->check_soc_is_end_charge(bms_device)) ||
        //                 ems->fully_charged_confirm(bms_device)) {
        //                 demand_allow_charge_ = false;
        //             }

        //             if (!demand_allow_charge_) {
        //                 // 如果正在充电，停止充电 - 使用线程安全的 getValue
        //                 double pcs_power = pcs1_device->getValue<double>("下设有功充电/放电功率", 0);
        //                 if (pcs_power < 0) {
        //                     pcs15am_cmd_->pcs_set_power(0, "需求响应", pcs1_device);
        //                 }

        //                 // 清除无功功率
        //                 double q_power = pcs1_device->getValue<double>("无功功率补偿功率设置", 0);
        //                 if (q_power != 0) {
        //                     pcs15am_cmd_->pcs_set_reactivePower(0, "需求响应", pcs1_device);
        //                 }

        //                 // 检查是否回落到回差范围
        //                 if (ems->check_charge_rd_recover(bms_device)) {
        //                     demand_allow_charge_ = true;
        //                 }
        //             } else {
        //                 // 设置有功功率
        //                 pcs15am_cmd_->pcs_set_power(static_cast<int>(ems->demandPower_need),
        //                                       "需求响应", pcs1_device);

        //                 // 设置无功功率
        //                 pcs15am_cmd_->pcs_set_reactivePower(reactive_power, "需求响应", pcs1_device);

        //                 // 开启PCS - 使用线程安全的 getValue
        //                 double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
        //                 if (pcs_switch != 1) {
        //                     pcs15am_cmd_->pcs_on_off("on", "需求响应", pcs1_device);
        //                 }
        //             }
        //         } else {
        //             // 放电模式
        //             ems->sys_running_pos.store(41);
        //             ems->demandPower_need = ems->get_max_discharge_power(demand_power_need);

        //             // 检查保护条件 - 使用线程安全的 getValue
        //             if ((ems->getValue<double>("使能单体电压保护", 0) > 0.5 &&
        //                  ems->fall_to_setting_lower_cell_voltage(bms_device)) ||
        //                 (ems->getValue<double>("使能SOC保护", 0) > 0.5 &&
        //                  ems->check_soc_is_end_discharge(bms_device)) ||
        //                 ems->fully_discharged_confirm(bms_device)) {
        //                 demand_allow_discharge_ = false;

        //                 double pcs_power = pcs1_device->getValue<double>("下设有功充电/放电功率", 0);
        //                 if (pcs_power > 0) {
        //                     pcs15am_cmd_->pcs_set_power(0, "需求响应", pcs1_device);
        //                 }

        //                 // 清除无功功率
        //                 double q_power = pcs1_device->getValue<double>("无功功率补偿功率设置", 0);
        //                 if (q_power != 0) {
        //                     pcs15am_cmd_->pcs_set_reactivePower(0, "需求响应", pcs1_device);
        //                 }
        //             }

        //             if (!demand_allow_discharge_) {
        //                 // 如果正在放电，停止放电
        //                 double pcs_power = pcs1_device->getValue<double>("下设有功充电/放电功率", 0);
        //                 if (pcs_power > 0) {
        //                     pcs15am_cmd_->pcs_set_power(0, "需求响应", pcs1_device);
        //                 }

        //                 // 检查是否回落到回差范围
        //                 if (ems->check_discharge_rd_recover(bms_device)) {
        //                     demand_allow_discharge_ = true;
        //                 }
        //             } else {
        //                 // 设置有功功率
        //                 pcs15am_cmd_->pcs_set_power(static_cast<int>(ems->demandPower_need),
        //                                       "需求响应", pcs1_device);

        //                 // 设置无功功率
        //                 pcs15am_cmd_->pcs_set_reactivePower(reactive_power, "需求响应", pcs1_device);

        //                 // 开启PCS
        //                 double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
        //                 if (pcs_switch != 1) {
        //                     pcs15am_cmd_->pcs_on_off("on", "需求响应", pcs1_device);
        //                 }

        //             }
        //         }
        //     } else {
        //         // 不在需求响应时段，检查是否在定时时段
        //         auto [should_run_week_plan, week_plan_power_need] = ems->check_charge_status();

        //         if (should_run_week_plan) {
        //             // 执行定时逻辑（简化版，与weeklyPlanModeRun类似）
        //             if (week_plan_power_need < 0) {
        //                 ems->weekPlanPower_need = ems->get_max_charge_power(week_plan_power_need);
        //                 pcs15am_cmd_->pcs_set_power(static_cast<int>(ems->weekPlanPower_need),
        //                                       "定时", pcs1_device);

        //                 // 使用线程安全的 getValue
        //                 double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
        //                 if (pcs_switch != 1) {
        //                     pcs15am_cmd_->pcs_on_off("on", "定时", pcs1_device);
        //                 }

        //             } else if (week_plan_power_need > 0) {
        //                 ems->weekPlanPower_need = ems->get_max_discharge_power(week_plan_power_need);
        //                 pcs15am_cmd_->pcs_set_power(static_cast<int>(ems->weekPlanPower_need),
        //                                       "定时", pcs1_device);

        //                 double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
        //                 if (pcs_switch != 1) {
        //                     pcs15am_cmd_->pcs_on_off("on", "定时", pcs1_device);
        //                 }

        //             } else {
        //                 // 关闭PCS
        //                 double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
        //                 if (pcs_switch != 0) {
        //                     pcs15am_cmd_->pcs_on_off("off", "定时", pcs1_device);
        //                 }


        //             }
        //         } else {
        //             // 既不在需求响应时段也不在定时时段，关闭PCS
        //             ems->sys_running_pos.store(34);

        //             double pcs_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
        //             if (pcs_switch != 0) {
        //                 pcs15am_cmd_->pcs_on_off("off", "定时", pcs1_device);
        //             }


        //         }
        //     }
        // } else if (alarm_level == 2) {
        //     // 二级告警
        //     ems->sys_running_pos.store(21);
        //     alarmLv2Protect();

        //     if (!demand_fault_occurred_) {
        //         demand_fault_happen_time_ = std::chrono::steady_clock::now();
        //         demand_fault_occurred_ = true;
        //     }

        //     auto current_time = std::chrono::steady_clock::now();
        //     auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        //         current_time - demand_fault_happen_time_).count();

        //     // 60秒后自动恢复
        //     if (elapsed > 60) {
        //         pcs15am_cmd_->pcs_reset(pcs1_device, "定时");

        //         // 使用线程安全的 setValue
        //         ems->setValue<double>("系统告警等级", 0);
        //         demand_fault_occurred_ = false;

        //         LOG_INFO_LOC("需求响应模式系统二级告警自恢复完成");
        //         std::this_thread::sleep_for(std::chrono::seconds(1));
        //     }
        // } else {
        //     // 三级告警
        //     ems->sys_running_pos.store(22);
        //     alarmLv3Protect();

        //     // 使用线程安全的 setValue
        //     ems->setValue<double>("开机", 0);
        //     ems->setValue<double>("系统告警等级", 3);
        //     ems->setValue<double>("系统运行模式", 1);  // 返回手动模式

        //     LOG_INFO_LOC("三级告警，退出需求响应模式");
        // }

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("需求响应模式运行异常: " + std::string(e.what()));
    }
}

// ============================================================
// 模式迁移：进入模式时初始化状态，退出模式时清理
// ============================================================
void Strategy::enterMode(int mode) {
    auto ems = EMS::instance();
    switch (mode) {
        case RunMode::TIMER:
            timer_allow_charge_ = true;
            timer_allow_discharge_ = true;
            timer_fault_occurred_ = false;
            timer_fault_happen_time_ = std::chrono::steady_clock::time_point();
            ems->sys_running_pos.store(14);
            LOG_INFO_LOC("进入定时模式");
            break;
        case RunMode::DEMAND_RESPONSE:
            demand_allow_charge_ = true;
            demand_allow_discharge_ = true;
            demand_fault_occurred_ = false;
            demand_fault_happen_time_ = std::chrono::steady_clock::time_point();
            // sys_running_pos 19 在每周期内设置
            LOG_INFO_LOC("进入需求响应模式");
            break;
        default:
            break;
    }
}

void Strategy::exitMode(int mode) {
    switch (mode) {
        case RunMode::TIMER:
            // 定时模式退出时重置功率追踪，避免下个模式误判
            last_sent_pcs_power_ = 0;
            LOG_INFO_LOC("退出定时模式");
            break;
        case RunMode::DEMAND_RESPONSE:
            last_sent_pcs_power_ = 0;
            LOG_INFO_LOC("退出需求响应模式");
            break;
        default:
            break;
    }
}

// 热管理
void Strategy::thermalManager() {
    auto ems = EMS::instance();
    auto bms_device = device_manager_->getDeviceByName("gtbms485");
    auto cooler_device = device_manager_->getDeviceByName("xm_cooler1");
    
    if (!bms_device || !cooler_device) {
        return;
    }
    
    // 使用线程安全的 getValue 方法
    double max_temp = bms_device->getValue<double>("电池最高温度", 0);
    double cooler_start_temp = ems->getValue<double>("coolerStartBatTemp", 35.0);
    double cooler_stop_temp = ems->getValue<double>("coolerStopBatTemp", 30.0);
    double cooler_enable = cooler_device->getValue<double>("机组开关使能", 0);
    
    // 启动液冷机
    if (max_temp >= cooler_start_temp && cooler_enable != 1) {
        // TODO: 调用液冷机开启命令
        LOG_INFO_LOC("启动液冷机，电池温度: " + std::to_string(max_temp));
    }
    
    // 停止液冷机
    if (max_temp <= cooler_stop_temp && cooler_enable != 0) {
        // TODO: 调用液冷机关闭命令
        LOG_INFO_LOC("停止液冷机，电池温度: " + std::to_string(max_temp));
    }
}

// 水泵管理
void Strategy::waterPumpManager() {
    auto ems = EMS::instance();
    auto bms_device = device_manager_->getDeviceByName("gtbms485");
    
    if (!bms_device) {
        return;
    }
    
    // 使用线程安全的 getValue 方法
    double max_temp = bms_device->getValue<double>("电池最高温度", 0);
    double pump_start_temp = ems->getValue<double>("pumpStartBatTemp", 35.0);
    double pump_stop_temp = ems->getValue<double>("pumpStopBatTemp", 30.0);

    // 水泵温度监控（无DO硬件，仅记录日志）
    if (max_temp >= pump_start_temp) {
        LOG_INFO_LOC("电池温度达到水泵启动阈值: " + std::to_string(max_temp) + "°C (启动阈值: " + std::to_string(pump_start_temp) + "°C)");
    }
    if (max_temp <= pump_stop_temp) {
        LOG_INFO_LOC("电池温度降至水泵停止阈值: " + std::to_string(max_temp) + "°C (停止阈值: " + std::to_string(pump_stop_temp) + "°C)");
    }
}

// 轮询告警，返回告警等级（0=无告警，2=二级告警，3=三级告警）
uint16_t Strategy::pollingAlarm() {
    auto ems = EMS::instance();
    bool alarm_lv1 = false;
    bool alarm_lv2 = false;
    bool alarm_lv3 = false;
    
    // 遍历所有设备检查告警
    for (const auto& device_pair : device_manager_->device_map_) {
        const auto& device = device_pair.second;
        
        // 检查一级告警
        if (!device->alarm_level1.empty()) {
            for (const auto& alarm : device->alarm_level1.items()) {
                if (alarm.value().contains("value") && alarm.value()["value"].get<bool>()) {
                    // 一级告警处理（暂不处理）
                    alarm_lv1 = true;
                }
            }
        }
        
        // 检查二级告警
        if (!device->alarm_level2.empty()) {
            for (const auto& alarm : device->alarm_level2.items()) {
                if (alarm.value().contains("value") && alarm.value()["value"].get<bool>()) {
                    alarm_lv2 = true;
                }
            }
        }
        
        // PCS和BMS通讯故障为二级告警
        if (device->get_name().find("pcs") != std::string::npos || 
            device->get_name().find("bms") != std::string::npos) {
            if (!device->online_status) {
                alarm_lv2 = true;
            }
        }
        
        // 检查三级告警
        if (!device->alarm_level3.empty()) {
            for (const auto& alarm : device->alarm_level3.items()) {
                if (alarm.value().contains("value") && alarm.value()["value"].get<bool>()) {
                    alarm_lv3 = true;
                }
            }
        }
    }
    
    // 更新系统告警等级 - 使用线程安全的 setValue
    if (alarm_lv3) {
        ems->setValue<double>("系统告警等级", 3);
        return 3;
    } else if (alarm_lv2) {
        ems->setValue<double>("系统告警等级", 2);
        return 2;
    } else if (alarm_lv1) {
        ems->setValue<double>("系统告警等级", 1);
        return 1;
    } else {
        ems->setValue<double>("系统告警等级", 0);
        return 0;
    }
}

// 二级告警保护
void Strategy::alarmLv2Protect() {
    LOG_WARNING_LOC("系统发生二级告警，进入保护策略动作");
    
    auto pcs1_device = device_manager_->getDeviceByName("pcs1");
    auto ems = EMS::instance();
    
    try {
        if (pcs1_device) {
            // 使用线程安全的 getValue 方法
            double pcs1_switch = pcs1_device->getValue<double>("开机、关机指令", 0);
            if (pcs1_switch != 0) {
                pcs15am_cmd_->pcs_on_off("off", "保护", pcs1_device);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        
        // 重置上次发送的功率值
        last_sent_pcs_power_ = 0;
        
    } catch (const std::exception& e) {
        LOG_WARNING_LOC("系统二级告警保护策略执行失败: " + std::string(e.what()));
    }
    
}

// 三级告警保护
void Strategy::alarmLv3Protect() {
    LOG_WARNING_LOC("系统发生三级告警，进入保护策略动作");
    
    auto pcs1_device = device_manager_->getDeviceByName("pcs1");
    auto bms_device = device_manager_->getDeviceByName("gtbms485");
    auto ems = EMS::instance();
    
    try {
        if (pcs1_device) {
            pcs15am_cmd_->pcs_on_off("off", "保护", pcs1_device);
        }    
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // TODO: BMS下高压命令
        
        // 重置上次发送的功率值
        last_sent_pcs_power_ = 0;
        
    } catch (const std::exception& e) {
        LOG_WARNING_LOC("系统三级告警保护策略执行失败: " + std::string(e.what()));
    }
}


void Strategy::pilotLampShowThread(){
    auto ems = EMS::instance();
    std::this_thread::sleep_for(std::chrono::seconds(3));
    while (keep_running_) {
        bool isFault = (static_cast<int>(ems->getValue<double>("系统告警等级", 0.0)) > 1);
        // (DO3 fault lamp control removed: no GPIO hardware on this EMS)
        if (isFault)
            ems->setValue("系统状态", 6);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}