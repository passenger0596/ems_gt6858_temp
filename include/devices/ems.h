#ifndef EMS_H
#define EMS_H

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>  // 添加共享锁支持（读写锁）
#include "device.h"
#include "nlohmann/json.hpp"
#include "hiredis/hiredis.h"
#include "config.h"

using json = nlohmann::json;

class EMS : public Device {
public:
    

    static std::shared_ptr<EMS> instance() {
        static std::shared_ptr<EMS> inst(new EMS(), [](EMS*) {});
        return inst;
    }

    EMS(const EMS&) = delete;
    EMS& operator=(const EMS&) = delete;
    
    // 析构函数
    ~EMS();

    virtual void parse_rawdata(const std::vector<uint16_t>& data_list) override;
    virtual void read_data(ModbusClient& mb_client) override;
        
    // 更新EMS状态
    void update_ems();
    
    // 读取DI状态
    void read_di_status();
    
    // 控制DO输出
    void do_on_off(int num, const std::string& switch_state);
    
    // 比较和同步Modbus TCP保持寄存器与data_dict
    void compare_and_synchronized(int address);
    
    // 读取并解析JSON配置文件
    bool read_and_parse_jsonfile(const std::string& filename);
    
    // 写入JSON配置文件
    bool write_jsonfile(const json& data, const std::string& dataname="",const std::string& filename =Config::EMS_CONFIG_FILEPATH_JSON);

    // 写入JSON配置文件（无锁版本，调用者需确保已持有json_rwlock_）
    bool write_jsonfile_nolock(const json& data, const std::string& dataname="",const std::string& filename =Config::EMS_CONFIG_FILEPATH_JSON);
    
    // 写入定时模式或需求响应模式JSON文件
    bool write_timerJsonFile(const json& data,const std::string& filename =Config::EMS_CONFIG_FILEPATH_JSON);
    
    // 解析DI/DO告警
    void parse_dido();
    
    // 获取当前时间字符串
    std::string get_current_time_string();
    
    // 获取当前ISO时间字符串
    std::string get_current_iso_time_string();
    
    // GPIO方向设置
    void setup_gpio_direction(int gpio_num, const std::string& direction);
    
    // 定时保存设备数据
    void cycle_record_all();
    
    // 启动数据保存
    void start_data_recording();
    
    // 停止数据保存
    void stop_data_recording();
    
    // 添加要保存的设备
    void add_device_to_save(std::shared_ptr<Device> device);
    
    // 从Redis发布数据
    void publish_data_to_redis();
    
    // 重写基类的线程安全访问方法，使用 EMS 专用的 json_rwlock_
    json get_data_to_qt_safe() const override {
        std::shared_lock<std::shared_mutex> lock(this->json_rwlock_);
        return this->data_to_qt;
    }
    
    void set_data_to_qt_safe(const json& new_data) override {
        std::unique_lock<std::shared_mutex> lock(this->json_rwlock_);
        this->data_to_qt = new_data;
    }
    
    // 时间相关辅助函数
    std::pair<bool, int> check_charge_status();  // 检查是否应该充电
    std::pair<bool, int> check_discharge_status();  // 检查是否应该放电
    bool check_soc_is_end_discharge(std::shared_ptr<Device> bms_device);  // 检查SOC是否到达放电下限
    bool check_soc_is_end_charge(std::shared_ptr<Device> bms_device);  // 检查SOC是否到达充电上限
    bool reach_setting_upper_cell_voltage(std::shared_ptr<Device> bms_device);  // 检查是否达到设定上限单体电压
    bool fall_to_setting_lower_cell_voltage(std::shared_ptr<Device> bms_device);  // 检查是否回落到设定下限单体电压
    bool fully_charged_confirm(std::shared_ptr<Device> bms_device);  // 确认是否充满
    bool fully_discharged_confirm(std::shared_ptr<Device> bms_device);  // 确认是否放完
    int get_max_charge_power(int request_power);  // 获取最大充电功率
    int get_max_discharge_power(int request_power);  // 获取最大放电功率
    bool check_charge_rd_recover(std::shared_ptr<Device> bms_device);  // 检查充电回差恢复
    bool check_discharge_rd_recover(std::shared_ptr<Device> bms_device);  // 检查放电回差恢复
    std::tuple<bool, int, int> check_demand_response_status();  // 检查需求响应状态
    
    // 成员变量
    int sys_running_pos;          // 程序运行位置
    int heartbeat;                // 心跳包
    
    json timingModeSet;           // 定时模式设置
    json demandResponseModeSet;   // 需求响应模式设置
    
    std::unordered_map<std::string, RegisterData> tcp_data_dict;           // TCP数据字典
    json tcp_timingModeSet;       // TCP定时模式缓存
    json tcp_demandResponseModeSet; // TCP需求响应模式缓存
    
    // DI GPIO编号字典
    std::unordered_map<std::string, int> di_num_dict;
    
    // DI状态字典
    std::unordered_map<std::string, bool> di_status;
    
    // DO GPIO编号字典
    std::unordered_map<std::string, int> do_num_dict;
    
    // DO状态字典
    std::unordered_map<std::string, bool> do_status;
    
    // TCP命令字典
    json tcp_cmd;
    
    // 数据到QT的映射
    json data_to_qt;
    
    // 告警缓存
    std::unordered_map<std::string, bool> alarm_cached;
    
    // 是否首次解析DI/DO
    bool first_parse_dido;
    
    // 是否保存数据
    std::atomic<bool> is_save_data;
    
    // 要保存的设备列表
    std::vector<std::shared_ptr<Device>> devices_to_save;
    
    // 保存周期（秒）
    int save_dev_cycle;
    
    // 热更新标志
    bool hot_update_flag;
    
    // 上次EMS关机时间
    std::string last_ems_shutdown;
    
    // 系统功率需求
    bool shouldRunWeekPlan;
    double weekPlanPower_need;
    bool shouldRunDemandResponse;
    double demandPower_need;
    
    // Redis连接
    redisContext* redis_conn;
    
    // 读写锁，保护共享JSON数据的并发访问（C++17）
    // 允许多个线程同时读取，写入时互斥
    mutable std::shared_mutex json_rwlock_;
    mutable std::shared_mutex do_rwlock_;
    
private:
    // 构造函数
    EMS();
    // 从JSON文件加载数据字典
    bool load_data_dict_from_json(const std::string& filepath);
    
    // 从JSON文件加载TCP命令
    bool load_tcp_cmd_from_json(const std::string& filepath);
    
    // 初始化GPIO
    void init_gpio();
    
    // 初始化命令映射
    void init_cmd_mapping();
    
    // 数据保存线程
    std::thread data_save_thread;
    
    // 数据保存线程运行标志
    std::atomic<bool> data_save_thread_running;
};

#endif // EMS_H