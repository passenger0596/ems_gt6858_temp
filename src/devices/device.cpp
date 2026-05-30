#include "devices/device.h"
#include "utils/sqlcpp.h"
#include "log.h"
#include <sstream>
#include <iomanip>
#include <chrono>

// 获取当前时间字符串的辅助函数
static std::string get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Device::handle_alarm(const std::string& alarm_name, 
                         uint8_t level, 
                         bool status, 
                         const std::string& now) {
    // 根据告警级别选择对应的告警字典
    json* alarm_level_json = nullptr;
    std::string level_str;
    
    switch (level) {
        case 1: 
            alarm_level_json = &this->alarm_level1[alarm_name];
            level_str = "一级";
            break;
        case 2: 
            alarm_level_json = &this->alarm_level2[alarm_name];
            level_str = "二级";
            break;
        case 3: 
            alarm_level_json = &this->alarm_level3[alarm_name];
            level_str = "三级";
            break;
        default:
            LOG_WARNING_LOC(("Unknown alarm level for " + alarm_name).c_str());
            return;
    }
    
    // 初始化告警对象（如果不存在）
    if (alarm_level_json->is_null()) {
        (*alarm_level_json) = json::object();
        (*alarm_level_json)["value"] = false;
        (*alarm_level_json)["lastTriggerTime"] = "";
        (*alarm_level_json)["lastClearTime"] = "";
    }
    
    // 更新告警值
    (*alarm_level_json)["value"] = status;
    
    // 检测告警状态变化并记录到数据库
    auto cache_it = this->alarm_cached.find(alarm_name);
    if (cache_it == this->alarm_cached.end()) {
        // 首次初始化，设置缓存
        this->alarm_cached[alarm_name] = status;
        
        // 如果首次检测就是告警状态，也需要插入数据库
        if (status) {
            LOG_INFO_LOC(("Alarm detected on first check: " + alarm_name + " (Level: " + level_str + ")").c_str());
            
            // 插入告警历史记录
            AlarmHistory alarm_record;
            alarm_record.level = level_str;
            alarm_record.alarm_time = now;
            alarm_record.device_name = this->name_;
            alarm_record.description = alarm_name;
            alarm_record.recovery_time = "NA";
            
            if (!SQL_CPP.insertAlarmHistory(alarm_record)) {
                LOG_ERROR_LOC(("Failed to insert alarm history: " + SQL_CPP.getLastError()).c_str());
            }
            
            // 更新告警对象的触发时间
            (*alarm_level_json)["lastTriggerTime"] = now;
            (*alarm_level_json)["lastClearTime"] = "";
        }
    } else {
        bool cached_status = cache_it->second;
        
        if (status && !cached_status) {
            // 告警触发：从False变为True
            LOG_INFO_LOC(("Alarm triggered: " + alarm_name + " (Level: " + level_str + ")").c_str());
            
            // 插入告警历史记录
            AlarmHistory alarm_record;
            alarm_record.level = level_str;
            alarm_record.alarm_time = now;
            alarm_record.device_name = this->name_;
            alarm_record.description = alarm_name;
            alarm_record.recovery_time = "NA";
            
            if (!SQL_CPP.insertAlarmHistory(alarm_record)) {
                LOG_ERROR_LOC(("Failed to insert alarm history: " + SQL_CPP.getLastError()).c_str());
            }
            
            // 更新告警对象的触发时间
            (*alarm_level_json)["lastTriggerTime"] = now;
            (*alarm_level_json)["lastClearTime"] = "";
            
            // 更新缓存
            this->alarm_cached[alarm_name] = true;
            
        } else if (!status && cached_status) {
            // 告警恢复：从True变为False
            LOG_INFO_LOC(("Alarm recovered: " + alarm_name + " (Level: " + level_str + ")").c_str());
            
            // 更新告警恢复时间
            if (!SQL_CPP.updateAlarmRecoveryTime(now, this->name_, alarm_name, now)) {
                LOG_ERROR_LOC(("Failed to update alarm recovery time: " + SQL_CPP.getLastError()).c_str());
            }
            
            // 更新告警对象的清除时间
            (*alarm_level_json)["lastClearTime"] = now;
            
            // 更新缓存
            this->alarm_cached[alarm_name] = false;
        }
    }
    std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
    // 更新总数据中的告警状态
    this->data_to_qt[alarm_name] = *alarm_level_json;
}
