#ifndef STRATEGY_H
#define STRATEGY_H

#include "json.hpp"
#include <atomic>
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
        std::shared_ptr<EjPcsCmd> pcs_cmd_;
        std::shared_ptr<EjDcdcCmd> dcdc_cmd_;
        std::shared_ptr<GtbmsCmd> gtbms_cmd_;
        std::shared_ptr<HengduAcCmd> hengdu_ac_cmd_;
        std::shared_ptr<Hgm6100Cmd> dg_hgm6100_cmd_;


        
        // 运行模式函数
        void sysRun();
        void manualModeRun();
        void weeklyPlanModeRun();
        void demandResponseModeRun();
        void autoModeRun();
        
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
};






















#endif