#ifndef STRATEGY_H
#define STRATEGY_H

#include "json.hpp"
#include <atomic>
#include <chrono>
#include <memory>

#include "command.h"

using json = nlohmann::json;

class DeviceManager; // 前向声明
class Device;        // 前向声明


class Strategy {
    public:
        Strategy(std::shared_ptr<DeviceManager> device_manager);
        ~Strategy();
        void runningThread();
        void stopThread();
        
    private:
        std::shared_ptr<DeviceManager> device_manager_;
        std::atomic<bool> keep_running_{true};
        std::thread worker_thread_;   // ✨ 新增：保存线程对象
        std::thread pilot_lamp_show_thread_;     // 指示灯线程

        // 设备命令对象：用指针 + 构造函数体内初始化，注释掉即禁用，适配不同项目
        std::shared_ptr<EmsCmd> ems_cmd_;
        std::shared_ptr<EjPcs15AmCmd> pcs15am_cmd_;
        std::shared_ptr<GtbmsCmd> gtbms_cmd_;


        
        // 运行模式函数
        void sysRun();
        void manualModeRun();
        void weeklyPlanModeRun();
        void demandResponseModeRun();
        void autoModeRun();

        // 模式管理
        void enterMode(int mode);
        void exitMode(int mode);
        
        // 辅助函数
        void thermalManager();
        void waterPumpManager();
        uint16_t pollingAlarm();
        void alarmLv2Protect();
        void alarmLv3Protect();
        bool timerStartupProcess();
        bool demandResponseStartupProcess();

        void pilotLampShowThread();     // 指示灯线程函数

        double last_sent_pcs_power_{0.0};  // 记录上次发送的PCS功率值

        // 状态机模式追踪
        int last_mode_{-1};

        // 周计划/定时模式持久状态
        bool timer_allow_charge_{true};
        bool timer_allow_discharge_{true};
        std::chrono::steady_clock::time_point timer_fault_happen_time_{};
        bool timer_fault_occurred_{false};

        // 需求响应模式持久状态
        bool demand_allow_charge_{true};
        bool demand_allow_discharge_{true};
        std::chrono::steady_clock::time_point demand_fault_happen_time_{};
        bool demand_fault_occurred_{false};
};






















#endif