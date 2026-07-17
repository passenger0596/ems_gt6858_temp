#include "devices/device.h"
#include "utils/sqlcpp.h"
#include "log.h"
#include <sstream>
#include <iomanip>
#include <chrono>

static std::string get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

Device::~Device() {
    std::lock_guard<std::mutex> lock(alarm_timer_mtx_);
    for (auto& pair : alarm_timers_) {
        pair.second->cancel();
    }
    alarm_timers_.clear();
    alarm_pending_values_.clear();
}

void Device::handle_alarm(const std::string& alarm_name, 
                         uint8_t level, 
                         bool status, 
                         int debounce_ms) {
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

    if (alarm_level_json->is_null()) {
        (*alarm_level_json) = json::object();
        (*alarm_level_json)["value"] = false;
        (*alarm_level_json)["lastTriggerTime"] = "";
        (*alarm_level_json)["lastClearTime"] = "";
    }

    bool current_value = (*alarm_level_json)["value"].get<bool>();

    if (status == current_value) {
        std::lock_guard<std::mutex> lock(alarm_timer_mtx_);
        auto timer_it = alarm_timers_.find(alarm_name);
        if (timer_it != alarm_timers_.end()) {
            timer_it->second->cancel();
            alarm_timers_.erase(timer_it);
            alarm_pending_values_.erase(alarm_name);
        }
        return;
    }

    if (debounce_ms <= 0) {
        {
            std::lock_guard<std::mutex> lock(alarm_timer_mtx_);
            auto timer_it = alarm_timers_.find(alarm_name);
            if (timer_it != alarm_timers_.end()) {
                timer_it->second->cancel();
                alarm_timers_.erase(timer_it);
                alarm_pending_values_.erase(alarm_name);
            }
        }
        _confirm_alarm(alarm_name, level, status);
        return;
    }

    std::lock_guard<std::mutex> lock(alarm_timer_mtx_);

    auto pending_it = alarm_pending_values_.find(alarm_name);
    if (pending_it != alarm_pending_values_.end() && pending_it->second == status) {
        return;
    }

    auto timer_it = alarm_timers_.find(alarm_name);
    if (timer_it != alarm_timers_.end()) {
        timer_it->second->cancel();
        alarm_timers_.erase(timer_it);
    }

    alarm_pending_values_[alarm_name] = status;

    auto timer = std::make_shared<AlarmDebounceTimer>();
    Device* dev_ptr = this;
    std::string alarm_name_copy = alarm_name;
    uint8_t level_copy = level;
    bool status_copy = status;
    auto debounce_dur = std::chrono::milliseconds(debounce_ms);

    timer->thread_ = std::thread([wk = std::weak_ptr<AlarmDebounceTimer>(timer),
                                   dev_ptr,
                                   alarm_name_copy,
                                   level_copy,
                                   status_copy,
                                   debounce_dur]() {
        auto t = wk.lock();
        if (!t) return;
        std::unique_lock<std::mutex> lk(t->mtx_);
        if (t->cv_.wait_for(lk, debounce_dur) == std::cv_status::timeout) {
            if (!t->is_cancelled()) {
                dev_ptr->_confirm_alarm(alarm_name_copy, level_copy, status_copy);
            }
        }
    });

    alarm_timers_[alarm_name] = timer;
}

void Device::_confirm_alarm(const std::string& alarm_name,
                            uint8_t level,
                            bool status) {
    {
        std::lock_guard<std::mutex> lock(alarm_timer_mtx_);
        alarm_timers_.erase(alarm_name);
        alarm_pending_values_.erase(alarm_name);
    }

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

    if (alarm_level_json->is_null()) {
        (*alarm_level_json) = json::object();
        (*alarm_level_json)["value"] = false;
        (*alarm_level_json)["lastTriggerTime"] = "";
        (*alarm_level_json)["lastClearTime"] = "";
    }

    (*alarm_level_json)["value"] = status;

    auto cache_it = this->alarm_cached.find(alarm_name);
    if (cache_it == this->alarm_cached.end()) {
        this->alarm_cached[alarm_name] = status;
        
        if (status) {
            std::string now = get_current_time_string();
            LOG_INFO_LOC(("Alarm detected on first check: " + alarm_name + " (Level: " + level_str + ")").c_str());
            
            AlarmHistory alarm_record;
            alarm_record.level = level_str;
            alarm_record.alarm_time = now;
            alarm_record.device_name = this->name_;
            alarm_record.description = alarm_name;
            alarm_record.recovery_time = "NA";
            
            if (!SQL_CPP.insertAlarmHistory(alarm_record)) {
                LOG_ERROR_LOC(("Failed to insert alarm history: " + SQL_CPP.getLastError()).c_str());
            }
            
            (*alarm_level_json)["lastTriggerTime"] = now;
            (*alarm_level_json)["lastClearTime"] = "";
        }
    } else {
        bool cached_status = cache_it->second;
        
        if (status && !cached_status) {
            std::string now = get_current_time_string();
            LOG_INFO_LOC(("Alarm triggered: " + alarm_name + " (Level: " + level_str + ")").c_str());
            
            AlarmHistory alarm_record;
            alarm_record.level = level_str;
            alarm_record.alarm_time = now;
            alarm_record.device_name = this->name_;
            alarm_record.description = alarm_name;
            alarm_record.recovery_time = "NA";
            
            if (!SQL_CPP.insertAlarmHistory(alarm_record)) {
                LOG_ERROR_LOC(("Failed to insert alarm history: " + SQL_CPP.getLastError()).c_str());
            }
            
            (*alarm_level_json)["lastTriggerTime"] = now;
            (*alarm_level_json)["lastClearTime"] = "";
            
            this->alarm_cached[alarm_name] = true;
            
        } else if (!status && cached_status) {
            std::string now = get_current_time_string();
            LOG_INFO_LOC(("Alarm recovered: " + alarm_name + " (Level: " + level_str + ")").c_str());
            
            if (!SQL_CPP.updateAlarmRecoveryTime(now, this->name_, alarm_name, now)) {
                LOG_ERROR_LOC(("Failed to update alarm recovery time: " + SQL_CPP.getLastError()).c_str());
            }
            
            (*alarm_level_json)["lastClearTime"] = now;
            
            this->alarm_cached[alarm_name] = false;
        }
    }
    {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt[alarm_name] = status;
    }
}

// ═══════════════════════════════════════════════════════════════
// 控制指令去重/防抖（与 Python device.py should_skip_control / mark_control_sent 一致）
// ═══════════════════════════════════════════════════════════════

bool Device::should_skip_control(const std::string& control_key, double value, double interval_s) {
    try {
        // interval_s 无效或非正数：不做去重，直接标记并允许发送
        if (interval_s <= 0.0) {
            mark_control_sent(control_key, value);
            return false;
        }

        std::lock_guard<std::mutex> lock(control_cache_mtx_);

        auto it = control_cached_.find(control_key);
        if (it == control_cached_.end()) {
            // 首次发送该控制键
            control_cached_[control_key] = {value, std::chrono::steady_clock::now()};
            return false;
        }

        const auto& cached = it->second;
        if (cached.value != value) {
            // 值与上次不同，允许发送
            control_cached_[control_key] = {value, std::chrono::steady_clock::now()};
            return false;
        }

        // 值相同，检查时间间隔
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - cached.timestamp).count();

        if (elapsed < interval_s) {
            // 在间隔时间内，跳过
            return true;
        }

        // 超过间隔时间，允许发送并更新时间戳
        control_cached_[control_key] = {value, now};
        return false;

    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("should_skip_control 异常: " + std::string(e.what())).c_str());
        return false;  // 异常时允许发送，保证控制可用
    }
}

void Device::mark_control_sent(const std::string& control_key, double value) {
    try {
        std::lock_guard<std::mutex> lock(control_cache_mtx_);
        control_cached_[control_key] = {value, std::chrono::steady_clock::now()};
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("mark_control_sent 异常: " + std::string(e.what())).c_str());
    }
}

void Device::clear_control_cache(const std::string& control_key) {
    try {
        std::lock_guard<std::mutex> lock(control_cache_mtx_);
        if (control_key.empty()) {
            control_cached_.clear();
        } else {
            control_cached_.erase(control_key);
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("clear_control_cache 异常: " + std::string(e.what())).c_str());
    }
}