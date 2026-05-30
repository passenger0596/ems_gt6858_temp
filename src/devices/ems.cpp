#include "ems.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include "utils.h"
#include "log.h"



// 构造函数
EMS::EMS() : Device("ems", 100, 0) {
    // 初始化基本属性
    this->sys_running_pos = 0;      // 程序运行位置
    this->heartbeat = 0;        // 心跳
    this->first_parse_dido = true;      // 初次解析dido
    this->is_save_data = false;         // 是否实时保存数据
    this->save_dev_cycle = 15;          // 保存周期
    this->hot_update_flag = false;      // 热更新标志
    this->last_ems_shutdown = "";
    this->shouldRunWeekPlan = false;        // 是否运行周计划
    this->weekPlanPower_need = 0;          // 周计划需求功率
    this->shouldRunDemandResponse = false;  // 是否运行需求响应
    this->demandPower_need = 0;           // 需求响应需求功率
    this->data_save_thread_running = false;     // 数据保存线程运行标志
    this->redis_conn = nullptr;         // Redis连接指针
    
    // 初始化在线状态
    this->online_status = 1;
    
    // 初始化GPIO字典
    this->di_num_dict = {{"DI1", 45}, {"DI2", 44}, {"DI3", 46}, {"DI4", 47}};
    this->do_num_dict = {{"DO1", 59}, {"DO2", 56}, {"DO3", 58}, {"DO4", 50}};
    
    // 初始化DI/DO状态
    for (const auto& di : this->di_num_dict) {
        this->di_status[di.first] = false;
    }
    for (const auto& d_o : this->do_num_dict) {
        this->do_status[d_o.first] = false;
    }
    
    // 初始化告警缓存
    init_config(Config::EMS_COMMUNICATION_FILEPATH);  // 从配置文件加载告警信息
    // this->alarm_cached = {
    //     {"消防主机故障", false},
    //     {"消防二级故障", false},
    //     {"水浸", false}
    // };
    
    // 初始化命令映射
    
    // 加载数据字典
    if (!load_data_dict_from_json(Config::EMS_DATA_DICT_FILEPATH)) {
        LOG_ERROR_LOC("EMS加载数据字典：" + Config::EMS_DATA_DICT_FILEPATH + "失败！");
    }
    
    // 加载TCP命令
    if (!load_tcp_cmd_from_json(Config::EMS_TCP_CMD_FILEPATH)) {
        LOG_ERROR_LOC("EMS加载TCP配置文件：" + Config::EMS_TCP_CMD_FILEPATH + "失败！");
    }
    
    // 加载配置文件
    if (!read_and_parse_jsonfile(Config::EMS_CONFIG_FILEPATH_JSON)) {
        LOG_ERROR_LOC("EMS加载配置文件：" + Config::EMS_CONFIG_FILEPATH_JSON + "失败！");
    }
    
    // 初始化TCP数据字典和模式设置
    this->tcp_data_dict = this->data_dict_;
    this->tcp_timingModeSet = this->timingModeSet;
    this->tcp_demandResponseModeSet = this->demandResponseModeSet;
    
    // 初始化GPIO
    init_gpio();
    
    // 初始化data_to_qt
    this->data_to_qt = {
        {"name", "ems"},
        {"online_status", 1},
        {"timestamp", ""},
        {"data", json::array()},
        {"timingModeSet", this->timingModeSet},
        {"demandResponseModeSet", this->demandResponseModeSet}
    };
    
    // 启动数据保存线程
    this->data_save_thread_running = true;
    this->data_save_thread = std::thread(&EMS::cycle_record_all, this);
    
    // 连接Redis
    this->redis_conn = redisConnect("127.0.0.1", 6379);
    if (this->redis_conn == nullptr || this->redis_conn->err) {
        LOG_ERROR_LOC("Failed to connect to Redis");
    }

    // 使用线程安全的 setValue 方法初始化系统状态
    this->setValue<double>("系统运行模式", 1);  // 设置系统运行模式为手动
    this->setValue<double>("系统状态", 2);  // 设置系统状态为待机
    
    LOG_INFO_LOC("EMS initialized successfully");
}

// 析构函数
EMS::~EMS() {
    // 停止数据保存线程
    this->data_save_thread_running = false;
    if (this->data_save_thread.joinable()) {
        this->data_save_thread.join();
    }
    
    // 关闭Redis连接
    if (this->redis_conn != nullptr) {
        redisFree(this->redis_conn);
    }
}

// 在 ems.cpp 中添加
void EMS::parse_rawdata(const std::vector<uint16_t>& data_list) {
    // EMS 不需要解析原始数据，但为了满足抽象类要求，实现空函数
    // 或者根据实际需求实现
    LOG_DEBUG_LOC("EMS parse_rawdata called");
}

void EMS::read_data(ModbusClient& mb_client) {
    // EMS 不需要从 Modbus 读取数据，但为了满足抽象类要求，实现空函数
    // 或者根据实际需求实现
    LOG_DEBUG_LOC("EMS read_data called");
}

// 从JSON文件加载数据字典
bool EMS::load_data_dict_from_json(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR_LOC("Cannot open data dict file: " + filepath);
            return false;
        }
        
        json data_dict_json;
        file >> data_dict_json;
        file.close();
        
        // 清空现有数据字典
        this->data_dict_.clear();
        
        // 遍历JSON对象，加载到data_dict_
        // 初始化阶段直接访问内部结构，无需加锁
        for (auto& item : data_dict_json.items()) {
            const std::string& key = item.key();
            json value_obj = item.value();
            
            RegisterData reg_data;
            reg_data.value = value_obj["value"].is_number() ? value_obj["value"].get<double>() : 0.0;
            reg_data.datatype = value_obj["datatype"].is_string() ? value_obj["datatype"].get<std::string>() : "";
            reg_data.mag = value_obj["mag"].is_number() ? value_obj["mag"].get<int>() : 1;
            reg_data.offset = value_obj["offset"].is_number() ? value_obj["offset"].get<int>() : 0;
            reg_data.unit = value_obj["unit"].is_string() ? value_obj["unit"].get<std::string>() : "";
            
            // 直接赋值到内部结构(初始化阶段,无需加锁)
            this->data_dict_[key] = reg_data;
        }
        
        LOG_INFO_LOC("Loaded " + std::to_string(this->data_dict_.size()) + " items from data dict JSON");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error loading data dict from JSON: " + std::string(e.what()));
        return false;
    }
}

// 从JSON文件加载TCP命令
bool EMS::load_tcp_cmd_from_json(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            LOG_ERROR_LOC("Cannot open TCP cmd file: " + filepath);
            return false;
        }
        
        file >> this->tcp_cmd;
        file.close();
        
        LOG_INFO_LOC("Loaded TCP commands from JSON");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error loading TCP commands from JSON: " + std::string(e.what()));
        return false;
    }
}

// 初始化GPIO
void EMS::init_gpio() {
    // 初始化所有DI为输入方向
    for (const auto& di : this->di_num_dict) {
        setup_gpio_direction(di.second, "in");
    }
    
    // 初始化所有DO为输出方向
    for (const auto& d_o : this->do_num_dict) {
        setup_gpio_direction(d_o.second, "out");
    }
    
    LOG_INFO_LOC("GPIO initialized");
}

// 更新EMS状态（写操作，使用独占锁）
void EMS::update_ems() {
    // 使用独占锁（写锁），因为会修改 data_dict_ 和 data_to_qt
    std::unique_lock<std::shared_mutex> lock(this->json_rwlock_);
    
    // 读取DI状态
    read_di_status();
    
    // 更新在线状态
    this->online_status = 1;
    
    // 更新data_to_qt
    this->data_to_qt["timestamp"] = get_current_time_string();
    
    // 构建临时数据列表
    std::vector<double> temp_list;
    
    // 添加基本状态
    temp_list.push_back(this->getValue<double>("开机", 0));
    temp_list.push_back(this->getValue<double>("系统运行模式", 1));
    temp_list.push_back(this->getValue<double>("系统状态", 2));
    temp_list.push_back(this->getValue<double>("系统告警等级", 0));
    
    // 添加DO状态
    for (const auto& do_item : this->do_status) {
        temp_list.push_back(do_item.second ? 1 : 0);
        // 使用线程安全的 setValue 方法
        this->setValue<double>(do_item.first, do_item.second ? 1.0 : 0.0);
    }
    
    // 添加DI状态
    for (const auto& di_item : this->di_status) {
        temp_list.push_back(di_item.second ? 1 : 0);
        // 使用线程安全的 setValue 方法
        this->setValue<double>(di_item.first, di_item.second ? 1.0 : 0.0);
    }
    
    // 添加系统并离网状态
    temp_list.push_back(this->getValue<double>("系统并离网", 0));
    
    // 更新data_to_qt的数据字段
    this->data_to_qt["data"] = temp_list;
    
    // 更新定时模式和需求响应模式
    this->data_to_qt["timingModeSet"] = this->timingModeSet;
    this->data_to_qt["demandResponseModeSet"] = this->demandResponseModeSet;
    
    // 更新告警
    parse_dido();
}

// 读取DI状态
void EMS::read_di_status() {
    try {
        for (const auto& di_item : this->di_num_dict) {
            const std::string& di_name = di_item.first;
            int gpio_num = di_item.second;
            
            std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_num) + "/value";
            std::ifstream file(gpio_path);
            
            if (!file.is_open()) {
                LOG_ERROR_LOC("Cannot open GPIO file: " + gpio_path);
                continue;
            }

            std::string value;
            file >> value;
            file.close();
            
            // 去除空白字符
            value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
            value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
            value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
            
            // 转换为布尔值
            bool di_value = (value == "1");
            this->di_status[di_name] = di_value;
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error reading DI status: " + std::string(e.what()));
    }
}

// 控制DO输出
void EMS::do_on_off(int num, const std::string& switch_state) {
    try {
        std::string do_name = "DO" + std::to_string(num);
        
        if (this->do_num_dict.find(do_name) == this->do_num_dict.end()) {
            LOG_ERROR_LOC("DO" + std::to_string(num) + " not found in DO dictionary");
            return;
        }
        
        int gpio_num = this->do_num_dict[do_name];
        std::string value = (switch_state == "on") ? "1" : "0";
        
        std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_num) + "/value";
        std::ofstream file(gpio_path);
        
        if (!file.is_open()) {
            LOG_ERROR_LOC("Cannot open GPIO file: " + gpio_path);
            return;
        }
        
        file << value;
        file.close();
        
        // 更新状态字典
        {
            std::lock_guard<std::shared_mutex> lock(this->do_rwlock_);
            this->do_status[do_name] = (switch_state == "on");
        }
        
        LOG_INFO_LOC("DO" + std::to_string(num) + " 设置为 " + switch_state);
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error controlling DO: " + std::string(e.what()));
    }
}

// GPIO方向设置
void EMS::setup_gpio_direction(int gpio_num, const std::string& direction) {
    try {
        std::string gpio_path = "/sys/class/gpio/gpio" + std::to_string(gpio_num) + "/direction";
        std::ofstream file(gpio_path);
        
        if (!file.is_open()) {
            LOG_ERROR_LOC("Cannot open GPIO direction file: " + gpio_path);
            return;
        }
        
        if (direction == "out") {
            file << "out";
        } else {
            file << "in";
        }
        
        file.close();
        LOG_INFO_LOC("GPIO " + std::to_string(gpio_num) + " direction set to " + direction);
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error setting GPIO direction: " + std::string(e.what()));
    }
}

// 读取并解析JSON配置文件（写操作，使用独占锁）
bool EMS::read_and_parse_jsonfile(const std::string& filename) {
    try {
        // 使用独占锁（写锁），因为会修改 timingModeSet、demandResponseModeSet 和 data_dict_
        std::unique_lock<std::shared_mutex> lock(this->json_rwlock_);
        
        std::ifstream file(filename);
        if (!file.is_open()) {
            LOG_ERROR_LOC("Cannot open config file: " + filename);
            return false;
        }
        
        json cfg_data;
        file >> cfg_data;
        file.close();
        
        // 更新data_dict中的配置值
        for (auto& item : cfg_data.items()) {
            const std::string& key = item.key();
            
            if (key == "timingModeSet") {
                this->timingModeSet = item.value();
            } else if (key == "demandResponseModeSet") {
                this->demandResponseModeSet = item.value();
            } else if (this->data_dict_.find(key) != this->data_dict_.end()) {
                // 更新data_dict中的值 - 使用线程安全的 setValue
                if (item.value().is_object() && item.value().contains("value")) {
                    double value = item.value()["value"].is_number() ? 
                                  item.value()["value"].get<double>() : 0.0;
                    this->setValue<double>(key, value);
                }
            }
        }
        
        LOG_INFO_LOC("EMS加载配置文件： " + filename + "成功！");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("EMS加载配置文件失败： " + std::string(e.what()));
        return false;
    }
}

// 写入JSON配置文件（无锁版本，调用者需确保已持有json_rwlock_）
bool EMS::write_jsonfile_nolock(const json& data, const std::string& dataname,const std::string& filename) {
    try {
        // 读取现有配置文件
        std::ifstream in_file(filename);
        json config_data;

        if (in_file.is_open()) {
            in_file >> config_data;
            in_file.close();
        } else {
            LOG_WARNING_LOC("Config file not found, will create new: " + filename);
        }

        // 更新数据
        if (!dataname.empty() && (data.is_number() || data.is_boolean())) {
            // 单个数值
            if (config_data.contains(dataname)) {
                config_data[dataname]["value"] = data;
                // 使用线程安全的 setValue
                double value = data.is_number() ? data.get<double>() :
                              (data.get<bool>() ? 1.0 : 0.0);
                this->setValue<double>(dataname, value);
            }
        } else if (data.is_object()) {
            // 字典数据
            for (auto& item : data.items()) {
                const std::string& key = item.key();
                if (config_data.contains(key)) {
                    config_data[key]["value"] = item.value();
                    if (this->data_dict_.find(key) != this->data_dict_.end()) {
                        // 使用线程安全的 setValue
                        double value = item.value().is_number() ?
                                      item.value().get<double>() : 0.0;
                        this->setValue<double>(key, value);
                    }
                }else {
                    LOG_WARNING_LOC("Key not found in ems_config_params: " + key);
                    return false;
                }
            }
        }

        // 添加时间戳
        config_data["timestamp"] = get_current_time_string();

        // 写入文件
        std::ofstream out_file(filename);
        if (!out_file.is_open()) {
            LOG_ERROR_LOC("Cannot write to config file: " + filename);
            return false;
        }

        out_file << config_data.dump(4);
        out_file.close();

        LOG_INFO_LOC(data.dump(4) + " 写入到JSON: " + filename);
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error writing config to JSON: " + std::string(e.what()));
        return false;
    }
}

// 写入JSON配置文件（写操作，使用独占锁）
bool EMS::write_jsonfile(const json& data, const std::string& dataname,const std::string& filename) {
    std::unique_lock<std::shared_mutex> lock(this->json_rwlock_);
    return write_jsonfile_nolock(data, dataname, filename);
}

// 写入定时模式或需求响应模式JSON文件（写操作，使用独占锁）
bool EMS::write_timerJsonFile(const json& data,const std::string& filename) {
    try {
        // 使用独占锁（写锁），确保写入时互斥
        std::unique_lock<std::shared_mutex> lock(this->json_rwlock_);
        
        // 读取现有配置文件
        std::ifstream in_file(filename);
        json config_data;
        
        if (in_file.is_open()) {
            in_file >> config_data;
            in_file.close();
        } else {
            LOG_WARNING_LOC("Config file not found, will create new: " + filename);
        }
        
        // 更新数据
        if (data.is_object()) {
            for (auto& item : data.items()) {
                const std::string& key = item.key();
                config_data[key] = item.value();
                
                // 同步更新内存中的JSON对象
                if (key == "timingModeSet") {
                    this->timingModeSet = item.value();
                } else if (key == "demandResponseModeSet") {
                    this->demandResponseModeSet = item.value();
                }
            }
        }
        
        // 添加时间戳
        config_data["timestamp"] = get_current_time_string();
        
        // 写入文件
        std::ofstream out_file(filename);
        if (!out_file.is_open()) {
            LOG_ERROR_LOC("Cannot write to config file: " + filename);
            return false;
        }
        
        out_file << config_data.dump(4);
        out_file.close();
        
        LOG_INFO_LOC("Timer config written to JSON: " + filename);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error writing timer config to JSON: " + std::string(e.what()));
        return false;
    }
}

// 比较和同步Modbus TCP保持寄存器与data_dict（写操作，使用独占锁）
void EMS::compare_and_synchronized(int address) {
    // 使用独占锁（写锁），因为会修改 data_dict_
    std::unique_lock<std::shared_mutex> lock(this->json_rwlock_);
    
    // 比较data_dict和tcp_data_dict
    bool data_dict_changed = false;
    
    for (auto& item : this->tcp_cmd.items()) {
        const std::string& key = item.key();
        int tcp_addr = item.value()["tcp_addr"].get<int>();
        
        if (tcp_addr == address - 1) {
            if (this->data_dict_.find(key) != this->data_dict_.end() && 
                this->tcp_data_dict.find(key) != this->tcp_data_dict.end()) {
                
                double data_dict_value = this->getValue<double>(key, 0);
                double tcp_data_value = this->tcp_data_dict[key].value;
                
                if (std::abs(data_dict_value - tcp_data_value) > 0.001) {
                    // 使用线程安全的 setValue（虽然已有锁，但保持一致性）
                    this->setValue<double>(key, tcp_data_value);
                    data_dict_changed = true;
                }
            }
        }
    }
    
    // 如果数据字典发生变化，更新配置文件
    if (data_dict_changed) {
        // 读取现有配置文件
        std::ifstream in_file(Config::EMS_CONFIG_FILEPATH_JSON);
        if (in_file.is_open()) {
            json config_data;
            in_file >> config_data;
            in_file.close();
            
            // 更新配置值 - 在锁保护下遍历
            {
                std::shared_lock<std::shared_mutex> lock(this->json_rwlock_);
                for (const auto& item : this->data_dict_) {
                    const std::string& key = item.first;
                    if (config_data.contains(key)) {
                        config_data[key]["value"] = item.second.value;
                    }
                }
            }
            
            // 写入文件
            std::ofstream out_file(Config::EMS_CONFIG_FILEPATH_JSON);
            out_file << config_data.dump(4);
            out_file.close();
            
            LOG_INFO_LOC("Data dictionary synchronized with TCP registers");
        }
    }
    
    // 比较定时模式
    if (this->timingModeSet != this->tcp_timingModeSet) {
        this->timingModeSet = this->tcp_timingModeSet;
        json to_set_dict = {{"timingModeSet", this->timingModeSet}};
        write_timerJsonFile(Config::EMS_CONFIG_FILEPATH_JSON, to_set_dict);
        LOG_INFO_LOC("Timing mode synchronized with TCP");
    }
    
    // 比较需求响应模式
    if (this->demandResponseModeSet != this->tcp_demandResponseModeSet) {
        this->demandResponseModeSet = this->tcp_demandResponseModeSet;
        json to_set_dict = {{"demandResponseModeSet", this->demandResponseModeSet}};
        write_timerJsonFile(Config::EMS_CONFIG_FILEPATH_JSON, to_set_dict);
        LOG_INFO_LOC("Demand response mode synchronized with TCP");
    }
}

// 解析DI/DO告警
void EMS::parse_dido() {
    // 注意：此函数应该在 update_ems 的锁保护下调用，所以不需要再次加锁
    
    // 获取当前时间字符串
    std::string now = Utils::getCurrentTimeString();
    
    // 首次执行时，只更新缓存，不触发告警
    if (this->first_parse_dido) {
        this->alarm_cached["消防主机故障"] = false;
        this->alarm_cached["消防二级故障"] = false;
        this->alarm_cached["水浸"] = false;
        this->first_parse_dido = false;
        return;
    }

    // ✅ 使用基类的通用告警处理方法处理DI告警
    
    // 消防主机故障（一级告警，常闭接点，需要取反）
    bool fire_host_fault = !this->di_status["DI1"];
    this->handle_alarm("消防主机故障", 1, fire_host_fault, now);
    
    // 消防二级故障（二级告警）
    bool fire_level2_fault = this->di_status["DI2"];
    this->handle_alarm("消防二级故障", 2, fire_level2_fault, now);
    
    // 水浸（三级告警）
    bool water_immersion = this->di_status["DI3"];
    this->handle_alarm("水浸", 3, water_immersion, now);
}

// 获取当前时间字符串
std::string EMS::get_current_time_string() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// 获取当前ISO时间字符串
std::string EMS::get_current_iso_time_string() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// 定时保存设备数据
void EMS::cycle_record_all() {
    std::unordered_map<std::string, std::ofstream> file_handles;
    std::unordered_map<std::string, bool> file_exists;
    
    while (this->data_save_thread_running) {
        try {
            if (this->is_save_data) {
                LOG_INFO_LOC("Saving data... Cycle: " + std::to_string(this->save_dev_cycle) + " seconds");
                
                for (auto& dev : this->devices_to_save) {
                    if (!dev) {
                        continue;
                    }
                    
                    std::string dev_name = dev->get_name();
                    if (dev_name.empty()) {
                        LOG_ERROR_LOC("Device has no name property!");
                        continue;
                    }
                    
                    // 确保记录目录存在
                    std::string records_dir = "./records/";
                    if (!std::filesystem::exists(records_dir)) {
                        std::filesystem::create_directories(records_dir);
                        LOG_INFO_LOC("Created records directory: " + records_dir);
                    }
                    
                    std::string csv_file = records_dir + dev_name + ".csv";
                    
                    // 如果文件句柄不存在，创建并初始化文件
                    if (file_handles.find(dev_name) == file_handles.end()) {
                        // 检查文件是否存在
                        bool file_exist = std::filesystem::exists(csv_file);
                        file_exists[dev_name] = file_exist;
                        
                        // 打开文件（追加模式）
                        std::ofstream file(csv_file, std::ios::app);
                        if (!file.is_open()) {
                            LOG_ERROR_LOC("Cannot open file: " + csv_file);
                            continue;
                        }
                        
                        file_handles[dev_name] = std::move(file);
                        
                        // 如果文件不存在，写入表头
                        if (!file_exist) {
                            LOG_INFO_LOC("Creating new CSV file: " + csv_file);
                            
                            // 准备表头
                            std::vector<std::string> headers = {"time"};
                            for (const auto& item : dev->data_dict_) {
                                std::string unit = item.second.unit;
                                std::string header = item.first;
                                if (!unit.empty()) {
                                    header += "(" + unit + ")";
                                }
                                headers.push_back(header);
                            }
                            
                            // 写入表头
                            std::ofstream& file_ref = file_handles[dev_name];
                            for (size_t i = 0; i < headers.size(); ++i) {
                                file_ref << headers[i];
                                if (i < headers.size() - 1) {
                                    file_ref << ",";
                                }
                            }
                            file_ref << "\n";
                            file_ref.flush();
                            
                            std::string header_log = "Header written: ";
                            for (const auto& h : headers) {
                                header_log += h + " ";
                            }
                            LOG_INFO_LOC(header_log);
                        }
                    }
                    
                    // 准备数据行
                    std::vector<std::string> values;
                    values.push_back(get_current_time_string());
                    
                    for (const auto& item : dev->data_dict_) {
                        values.push_back(std::to_string(item.second.value));
                    }
                    
                    // 写入数据行
                    std::ofstream& file_ref = file_handles[dev_name];
                    for (size_t i = 0; i < values.size(); ++i) {
                        file_ref << values[i];
                        if (i < values.size() - 1) {
                            file_ref << ",";
                        }
                    }
                    file_ref << "\n";
                    file_ref.flush();
                    
                    LOG_INFO_LOC("Data recorded: " + dev_name + " at " + values[0]);
                }
                
                // 等待下一个周期
                std::this_thread::sleep_for(std::chrono::seconds(this->save_dev_cycle));
                
            } else {
                // 停止保存数据，关闭所有文件句柄
                if (!file_handles.empty()) {
                    LOG_INFO_LOC("Stopping data recording, closing all file handles");
                    file_handles.clear();
                    file_exists.clear();
                }
                
                // 只等待，不进行记录
                std::this_thread::sleep_for(std::chrono::seconds(this->save_dev_cycle));
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR_LOC("Error saving device data: " + std::string(e.what()));
            
            // 发生异常时，关闭所有文件句柄
            file_handles.clear();
            file_exists.clear();
            
            std::this_thread::sleep_for(std::chrono::seconds(this->save_dev_cycle));
        }
    }
    
    // 线程结束时关闭所有文件句柄
    file_handles.clear();
    file_exists.clear();
    LOG_INFO_LOC("Data recording thread stopped");
}

// 启动数据保存
void EMS::start_data_recording() {
    this->is_save_data = true;
    LOG_INFO_LOC("Data recording started");
}

// 停止数据保存
void EMS::stop_data_recording() {
    this->is_save_data = false;
    LOG_INFO_LOC("Data recording stopped");
}

// 添加要保存的设备
void EMS::add_device_to_save(std::shared_ptr<Device> device) {
    this->devices_to_save.push_back(device);
    LOG_INFO_LOC("Device added to save list: " + device->get_name());
}

// 从Redis发布数据（只读操作，使用共享锁）
void EMS::publish_data_to_redis() {
    if (this->redis_conn == nullptr || this->redis_conn->err) {
        LOG_ERROR_LOC("Redis connection error");
        return;
    }
    
    json data_to_store;
    std::string serialized;
    
    try {
        // 使用共享锁（读锁），允许多个线程同时读取
        {
            std::shared_lock<std::shared_mutex> lock(this->json_rwlock_);
            
            // 构建要存储的数据
            data_to_store["name"] = "ems";
            data_to_store["online_status"] = this->online_status.load();
            data_to_store["timestamp"] = get_current_iso_time_string();
            
            // 构建data字典 - 创建副本以避免在序列化过程中被修改
            json data_dict_json;
            for (const auto& item : this->data_dict_) {
                json reg_json;
                reg_json["value"] = item.second.value;
                reg_json["unit"] = item.second.unit;
                data_dict_json[item.first] = reg_json;
            }
            data_to_store["data"] = data_dict_json;
            
            // 告警状态 - 创建副本
            data_to_store["alarm1_status"] = this->alarm_level1;
            data_to_store["alarm2_status"] = this->alarm_level2;
            data_to_store["alarm3_status"] = this->alarm_level3;
            
            // 定时模式和需求响应模式 - 创建副本
            data_to_store["timingModeSet"] = this->timingModeSet;
            data_to_store["demandResponseModeSet"] = this->demandResponseModeSet;
            
            // 序列化JSON（在锁保护下进行）
            serialized = data_to_store.dump();
        }  // 共享锁在此处自动释放
        
        // 发布到Redis（在锁外执行，避免长时间持有锁）
        std::string key = "device:ems";
        redisReply* reply = (redisReply*)redisCommand(this->redis_conn, "SETEX %s 20 %s", 
                                                     key.c_str(), serialized.c_str());
        
        if (reply == nullptr) {
            LOG_ERROR_LOC("Redis command failed");
        } else {
            freeReplyObject(reply);
            LOG_INFO_LOC("EMS data published to Redis");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Error publishing data to Redis: " + std::string(e.what()));
    }
}

// 解析时间字符串为小时和分钟
static std::pair<int, int> parse_time_string(const std::string& time_str) {
    try {
        size_t colon_pos = time_str.find(':');
        if (colon_pos == std::string::npos) {
            return {0, 0};
        }
        int hour = std::stoi(time_str.substr(0, colon_pos));
        int minute = std::stoi(time_str.substr(colon_pos + 1));
        return {hour, minute};
    } catch (...) {
        return {0, 0};
    }
}

// 检查是否应该充电
std::pair<bool, int> EMS::check_charge_status() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    int current_weekday = tm_now.tm_wday;  // 0=Sunday, 1=Monday, ..., 6=Saturday
    // 转换为Python的weekday格式 (0=Monday, 6=Sunday)
    current_weekday = (current_weekday == 0) ? 6 : (current_weekday - 1);
    
    int current_minutes = tm_now.tm_hour * 60 + tm_now.tm_min;
    
    if (!this->timingModeSet.contains("chargeTimeList") || !this->timingModeSet["chargeTimeList"].is_array()) {
        return {false, 0};
    }
    
    for (const auto& charge_period : this->timingModeSet["chargeTimeList"]) {
        if (!charge_period.contains("weekday") || !charge_period["weekday"].is_array()) {
            continue;
        }
        
        const auto& weekday_array = charge_period["weekday"];
        if (current_weekday >= 0 && current_weekday < static_cast<int>(weekday_array.size())) {
            std::string weekday_enabled = weekday_array[current_weekday].get<std::string>();
            
            if (weekday_enabled == "1") {
                std::string start_time_str = charge_period.value("startTime", "00:00");
                std::string end_time_str = charge_period.value("endTime", "00:00");
                int power = charge_period.value("power", 0);
                
                auto [start_hour, start_minute] = parse_time_string(start_time_str);
                auto [end_hour, end_minute] = parse_time_string(end_time_str);
                
                int start_minutes = start_hour * 60 + start_minute;
                int end_minutes = end_hour * 60 + end_minute + 1;  // 包含结束时间所在的那一分钟
                
                // 处理跨天情况
                if (end_minutes <= start_minutes) {
                    // 跨天：[start_minutes, 1440) 或 [0, end_minutes)
                    if (current_minutes >= start_minutes || current_minutes < end_minutes) {
                        return {true, power};
                    }
                } else {
                    // 不跨天
                    if (current_minutes >= start_minutes && current_minutes < end_minutes) {
                        return {true, power};
                    }
                }
            }
        }
    }
    
    return {false, 0};
}

// 检查是否应该放电
std::pair<bool, int> EMS::check_discharge_status() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    int current_weekday = tm_now.tm_wday;
    current_weekday = (current_weekday == 0) ? 6 : (current_weekday - 1);
    
    int current_minutes = tm_now.tm_hour * 60 + tm_now.tm_min;
    
    if (!this->timingModeSet.contains("dischargeTimeList") || !this->timingModeSet["dischargeTimeList"].is_array()) {
        return {false, 0};
    }
    
    for (const auto& discharge_period : this->timingModeSet["dischargeTimeList"]) {
        if (!discharge_period.contains("weekday") || !discharge_period["weekday"].is_array()) {
            continue;
        }
        
        const auto& weekday_array = discharge_period["weekday"];
        if (current_weekday >= 0 && current_weekday < static_cast<int>(weekday_array.size())) {
            std::string weekday_enabled = weekday_array[current_weekday].get<std::string>();
            
            if (weekday_enabled == "1") {
                std::string start_time_str = discharge_period.value("startTime", "00:00");
                std::string end_time_str = discharge_period.value("endTime", "00:00");
                
                auto [start_hour, start_minute] = parse_time_string(start_time_str);
                auto [end_hour, end_minute] = parse_time_string(end_time_str);
                
                int start_minutes = start_hour * 60 + start_minute;
                int end_minutes = end_hour * 60 + end_minute + 1;
                
                if (end_minutes <= start_minutes) {
                    if (current_minutes >= start_minutes || current_minutes < end_minutes) {
                        return {true, 0};
                    }
                } else {
                    if (current_minutes >= start_minutes && current_minutes < end_minutes) {
                        return {true, 0};
                    }
                }
            }
        }
    }
    
    return {false, 0};
}

// 检查SOC是否到达放电下限
bool EMS::check_soc_is_end_discharge(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    double current_soc = bms_device->getValue<double>("电池簇总SOC", -1);
    if (current_soc < 0) {
        return false;  // 数据无效
    }
    
    double end_discharge_soc = this->getValue<double>("停止放电SOC", 20.0);
    
    return current_soc <= end_discharge_soc;
}

// 检查SOC是否到达充电上限
bool EMS::check_soc_is_end_charge(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    double current_soc = bms_device->getValue<double>("电池簇总SOC", -1);
    if (current_soc < 0) {
        return false;  // 数据无效
    }
    
    double end_charge_soc = this->getValue<double>("停止充电SOC", 95.0);
    
    return current_soc >= end_charge_soc;
}

// 检查是否达到设定上限单体电压
bool EMS::reach_setting_upper_cell_voltage(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    double max_cell_voltage = bms_device->getValue<double>("最大单体电压(V)", 0);
    double end_charge_voltage = this->getValue<double>("停止充电单体电压", 3.65);
    
    return max_cell_voltage >= end_charge_voltage;
}

// 检查是否回落到设定下限单体电压
bool EMS::fall_to_setting_lower_cell_voltage(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    double min_cell_voltage = bms_device->getValue<double>("最小单体电压(V)", 0);
    double end_discharge_voltage = this->getValue<double>("停止放电单体电压", 2.8);
    
    return min_cell_voltage <= end_discharge_voltage;
}

// 确认是否充满
bool EMS::fully_charged_confirm(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    double alarm_value = bms_device->getValue<double>("单体电压过压2级报警", 0);
    
    return alarm_value > 0.5;  // 假设布尔值用double存储，>0.5表示true
}

// 确认是否放完
bool EMS::fully_discharged_confirm(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    double alarm_value = bms_device->getValue<double>("单体电压欠压2级报警", 0);
    return alarm_value > 0.5;
}

// 获取最大充电功率（负值）
int EMS::get_max_charge_power(int request_power) {
    // 使用线程安全的 getValue 方法
    int pcs_max_charge = static_cast<int>(this->getValue<double>("PCS最大充电功率", 50000));
    
    if (std::abs(request_power) >= pcs_max_charge) {
        return -pcs_max_charge;
    } else {
        return request_power;
    }
}

// 获取最大放电功率（正值）
int EMS::get_max_discharge_power(int request_power) {
    // 使用线程安全的 getValue 方法
    int pcs_max_discharge = static_cast<int>(this->getValue<double>("PCS最大放电功率", 50000));
    
    if (std::abs(request_power) >= pcs_max_discharge) {
        return pcs_max_discharge;
    } else {
        return request_power;
    }
}

// 检查充电回差恢复
bool EMS::check_charge_rd_recover(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    bool enable_soc_protect = this->getValue<double>("使能SOC保护", 0) > 0.5;
    bool enable_cell_vol_protect = this->getValue<double>("使能单体电压保护", 0) > 0.5;
    
    if (enable_soc_protect) {
        double current_soc = bms_device->getValue<double>("电池簇总SOC", -1);
        if (current_soc >= 0) {
            double end_charge_soc = this->getValue<double>("停止充电SOC", 95.0);
            double rd_end_charge_soc = this->getValue<double>("停止充电SOC回差", 2.0);
            
            if (current_soc <= (end_charge_soc - rd_end_charge_soc)) {
                return true;
            }
        }
    }
    
    if (enable_cell_vol_protect) {
        double max_voltage = bms_device->getValue<double>("最高单体电压", 0);
        double end_charge_voltage = this->getValue<double>("停止充电单体电压", 3.65);
        double rd_end_charge_voltage = this->getValue<double>("停止充电单体电压回差", 0.05);
        
        if (max_voltage <= (end_charge_voltage - rd_end_charge_voltage)) {
            return true;
        }
    }
    
    // 检查是否低于过压2级报警阈值-0.3V
    double alarm_threshold = bms_device->getValue<double>("单体电压过压2级报警阈值", 0);
    double max_voltage = bms_device->getValue<double>("最高单体电压", 0);
    
    if (alarm_threshold > 0 && max_voltage <= (alarm_threshold - 0.3)) {
        return true;
    }
    
    return false;
}

// 检查放电回差恢复
bool EMS::check_discharge_rd_recover(std::shared_ptr<Device> bms_device) {
    if (!bms_device) {
        return false;
    }
    
    // 使用线程安全的 getValue 方法
    bool enable_soc_protect = this->getValue<double>("使能SOC保护", 0) > 0.5;
    bool enable_cell_vol_protect = this->getValue<double>("使能单体电压保护", 0) > 0.5;
    
    if (enable_soc_protect) {
        double current_soc = bms_device->getValue<double>("电池簇总SOC", -1);
        if (current_soc >= 0) {
            double end_discharge_soc = this->getValue<double>("停止放电SOC", 20.0);
            double rd_end_discharge_soc = this->getValue<double>("停止放电SOC回差", 2.0);
            
            if (current_soc >= (end_discharge_soc + rd_end_discharge_soc)) {
                return true;
            }
        }
    }
    
    if (enable_cell_vol_protect) {
        double min_voltage = bms_device->getValue<double>("最低单体电压", 0);
        double end_discharge_voltage = this->getValue<double>("停止放电单体电压", 2.8);
        double rd_end_discharge_voltage = this->getValue<double>("停止放电单体电压回差", 0.05);
        
        if (min_voltage >= (end_discharge_voltage + rd_end_discharge_voltage)) {
            return true;
        }
    }
    
    // 检查是否高于欠压2级报警阈值+0.3V
    double alarm_threshold = bms_device->getValue<double>("单体电压欠压2级报警阈值", 0);
    double min_voltage = bms_device->getValue<double>("最低单体电压", 0);
    
    if (min_voltage >= (alarm_threshold + 0.3)) {
        return true;
    }
    
    return false;
}

// 检查需求响应状态
std::tuple<bool, int, int> EMS::check_demand_response_status() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&time_t_now);
    
    // 构造当前时间的datetime对象用于比较
    std::stringstream ss;
    ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M");
    std::string current_datetime_str = ss.str();
    
    if (!this->demandResponseModeSet.is_array()) {
        return {false, 0, 0};
    }
    
    for (const auto& demand_response : this->demandResponseModeSet) {
        if (!demand_response.contains("startDatetime") || !demand_response.contains("endDatetime")) {
            continue;
        }
        
        std::string start_datetime = demand_response["startDatetime"].get<std::string>();
        std::string end_datetime = demand_response["endDatetime"].get<std::string>();
        
        // 简单的字符串比较（假设格式一致）
        if (current_datetime_str >= start_datetime && current_datetime_str <= end_datetime) {
            int active_power = demand_response.value("activePower", 0);
            int reactive_power = demand_response.value("reactivePower", 0);
            return {true, active_power, reactive_power};
        }
    }
    
    return {false, 0, 0};
}


void EMS::init_config(const std::string& config_file){
    try{
        LOG_INFO_LOC("EMS开始初始化告警配置文件: " + config_file);
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(config_file.c_str());

        if (!result) {
            LOG_ERROR_LOC(("Failed to load config file: " + config_file + ", Error: " + result.description()).c_str());
            return;
        }

        pugi::xml_node root = doc.document_element();
        if (!root) {
            LOG_ERROR_LOC("Invalid XML format");
            return;
        }

        this->parse_alarm_config(root);
    }
    catch (...) {
        LOG_CRITICAL_LOC("Failed to load alarm from XML file: " + config_file);
        return;
    }


}