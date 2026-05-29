#include "devicemanager.h"
#include <iostream>
#include <cmath>
#include "config.h"
#include "utils.h"
#include "canoperator.h"
#include "modbusclient.h"
#include <hiredis/hiredis.h>
#include "log.h"
#include <thread>
#include <map>
#include <chrono>
#include <iomanip>
#include <functional>  // 用于 std::function

DeviceManager::DeviceManager() {
    // 初始化所有设备实例
    this->ems_ = EMS::instance();
    this->pcs_ = std::make_shared<Pcs_15am>("pcs1", 0, 1);
    this->wea1610_ = std::make_shared<Wea1610>("ac_wea1610", 2, 1);
    this->dehumidifierV2_ = std::make_shared<DehumidifierV2>("dehumidifier", 3, 1);
    this->dtsd3366_ = std::make_shared<ACMeter_3366>("dtsd3366", 4, 1);
    // this->bms_uhome_ = std::make_shared<BmsUhome>("bms_uhome", 16, 1);
    this->devices_ = {this->ems_, this->pcs_, this->wea1610_, this->dehumidifierV2_, this->dtsd3366_}; 
    for (auto& device : this->devices_) {
        this->device_map_[device->get_name()] = device;
    }

    // 初始化控制消息回调为空
    control_message_callback_ = nullptr;
}

DeviceManager::~DeviceManager() {
    stopModbusTcpServer();
    stopAllThreads();
}

// 设置控制消息回调
void DeviceManager::setControlMessageCallback(ControlMessageCallback callback) {
    control_message_callback_ = callback;
}

std::shared_ptr<Device> DeviceManager::getDeviceByName(const std::string& name)
{
    auto it = this->device_map_.find(name);

    if (it != this->device_map_.end()) {
        return it->second;
    }

    return nullptr;
}


std::unordered_map<int, std::shared_ptr<ModbusClient>> DeviceManager::getModbusClients() {  
    return this->mapComToModbusClient;
}

std::unordered_map<int, std::shared_ptr<CanOperator>> DeviceManager::getCanOperators() {
    return this->mapComToCanOperator;
}


void DeviceManager::createReadThreads()
{
    // 1. 将所有已注册设备按通信端口 (COM/CAN) 进行分组
    for(auto& device : this->devices_){
        uint8_t com_num = device->get_com();
        if (Config::SERIAL_PORTS.find(com_num) != Config::SERIAL_PORTS.end()) {
            this->dev_com_map[com_num].push_back(device); 
        } else if (Config::CAN_INTERFACES.find(com_num) != Config::CAN_INTERFACES.end()) {
            this->can_dev_map[com_num].push_back(device);
        }
    }
    // 2. 遍历串口 (Modbus) 端口，为每个端口启动一个读取线程
    for (auto& pair : this->dev_com_map){
        // 检查串口配置是否存在
        auto it = Config::SERIAL_PORTS.find(pair.first);
        if (it != Config::SERIAL_PORTS.end()) {
            std::shared_ptr<ModbusClient> modbus_client;
            if (it->first<=7){
                // 使用串口通信构造函数，明确使用默认波特率
                LOG_INFO_LOC(("创建ModbusClient: " + it->second + " (COM" + std::to_string(it->first) + ")").c_str());
                modbus_client = std::make_shared<ModbusClient>(std::string(it->second), 9600);
            }else{
                // 使用TCP通信构造函数
                LOG_INFO_LOC(("Creating ModbusClient for TCP: " + it->second).c_str());
                auto [ip,port] = Utils::splitIpPort(it->second);
                modbus_client = std::make_shared<ModbusClient>(ip, port);
            }
            
            // 尝试连接，如果失败则记录警告但继续运行
            bool connected = modbus_client->connect();
            if (!connected){
                LOG_WARNING_LOC(("ModbusClient connection 失败 for " + it->second + ", this device will be skipped").c_str());
                LOG_WARNING_LOC("Continuing with other devices...");
                continue;
            }
            
            LOG_INFO_LOC(("成功connect到" + it->second).c_str());
            
            // 创建线程特定的停止标志
            int thread_id = this->next_thread_id_++;
            this->thread_stop_flags_[thread_id] = false;
            this->thread_modbus_clients_[thread_id] = modbus_client;

            // 存储ModbusClient的映射
            this->mapComToModbusClient[static_cast<int>(pair.first)] = modbus_client;
            
            // 使用 lambda 创建线程，以便捕获线程特定的停止标志
            this->device_threads_.emplace_back(
                [this, com = pair.first, modbus_client, thread_id]() {
                    this->readDeviceThreadWithStopFlag(com, modbus_client, thread_id);
                }
            );
            LOG_INFO_LOC(("创建读取线程成功: " + it->second).c_str());
        } else {
            LOG_ERROR_LOC("Error: Serial port configuration not found for COM " + 
                         std::to_string(static_cast<int>(pair.first)));
        }
       
    }

    // 3. 遍历 CAN 接口，为每个接口启动一个读取线程
    for (auto& pair : this->can_dev_map) {
        auto it = Config::CAN_INTERFACES.find(pair.first);
        if (it == Config::CAN_INTERFACES.end()) {
            LOG_ERROR_LOC("Error: CAN interface configuration not found for COM " +
                         std::to_string(static_cast<int>(pair.first)));
            continue;
        }

        auto can_operator = std::make_shared<CanOperator>(it->second, 0);
        const bool connected = can_operator->connect();
        if (!connected) {
            LOG_WARNING_LOC(("CanOperator connection failed for " + it->second +
                             ", this device group will be skipped").c_str());
            continue;
        }

        const int thread_id = this->next_thread_id_++;
        this->thread_stop_flags_[thread_id] = false;
        this->thread_can_operators_[thread_id] = can_operator;
        this->mapComToCanOperator[static_cast<int>(pair.first)] = can_operator;

        this->device_threads_.emplace_back(
            [this, com = pair.first, can_operator, thread_id]() {
                this->readCanDeviceThreadWithStopFlag(com, can_operator, thread_id);
            }
        );
    }
}

void DeviceManager::readDeviceThreadWithStopFlag(
    uint8_t com,
    std::shared_ptr<ModbusClient> modbus_client,
    int thread_id)
{
    LOG_INFO_LOC(
        ("Modbus read thread started for COM" +
        std::to_string(static_cast<int>(com))).c_str());

    while (!this->stop_threads_.load())
    {
        // 不再持有 stop_flag 引用
        auto it = this->thread_stop_flags_.find(thread_id);

        // stop flag不存在
        // 说明正在退出
        if (it == this->thread_stop_flags_.end()) {
            break;
        }

        // 当前线程停止
        if (it->second.load()) {
            break;
        }

        // modbus client为空
        if (!modbus_client) {

            LOG_ERROR_LOC(
                ("Modbus client is null for COM" +
                std::to_string(static_cast<int>(com))).c_str());

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));

            continue;
        }

        // 遍历当前串口下所有设备
        for (auto& device : this->dev_com_map[com])
        {
            // 再次检查停止状态
            auto stop_it =
                this->thread_stop_flags_.find(thread_id);

            if (this->stop_threads_.load() ||
                stop_it == this->thread_stop_flags_.end() ||
                stop_it->second.load())
            {
                LOG_INFO_LOC(
                    ("Modbus thread exit for COM" +
                    std::to_string(static_cast<int>(com))).c_str());

                return;
            }

            // 空设备保护
            if (!device) {

                LOG_ERROR_LOC("device is nullptr");

                continue;
            }

            try {

                device->read_data(*modbus_client);

            }
            catch (const std::exception& e) {

                LOG_ERROR_LOC(
                    device->get_name() +
                    " read_data exception: " +
                    e.what());

            }
            catch (...) {

                LOG_ERROR_LOC(
                    device->get_name() +
                    " unknown exception");
            }

            // 小睡眠
            // 提高退出响应速度
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));
        }
    }

    LOG_INFO_LOC(
        ("Modbus read thread stopped for COM" +
        std::to_string(static_cast<int>(com))).c_str());
}

// void DeviceManager::readDeviceThreadWithStopFlag(uint8_t com, 
//     std::shared_ptr<ModbusClient> modbus_client, int thread_id)
// {
//     // 获取当前线程的停止标志引用
    

//     auto& stop_flag = this->thread_stop_flags_[thread_id];

//     while(!stop_flag && !this->stop_threads_)
//     {   
//         // 快速检查停止标志
//         if (stop_flag || this->stop_threads_) break;
        
//         // 读取同一个串口的每个设备的数据
//         for(auto& device : this->dev_com_map[com])
//         {
//             // 每次操作前都检查停止标志
//             if (stop_flag || this->stop_threads_) return;
            
//             device->read_data(*modbus_client);
//             // 使用更短的睡眠，以便更快响应停止信号
//             // std::this_thread::sleep_for(std::chrono::milliseconds(20));

//         }
//     }
// }

/**
 * @brief CAN 设备的线程主循环
 * 轮询该接口下挂载的所有 CAN 设备
 */
void DeviceManager::readCanDeviceThreadWithStopFlag(uint8_t com, 
    std::shared_ptr<CanOperator> can_operator, int thread_id)
{
    // 获取当前线程的停止标志引用
    auto& stop_flag = this->thread_stop_flags_[thread_id];
    
    LOG_INFO_LOC(("CAN read thread started for COM" + std::to_string(static_cast<int>(com))).c_str());

    while (!stop_flag && !this->stop_threads_) {
        // 依次读取该接口下的每个设备
        for (auto& device : this->can_dev_map[com]) {
            if (stop_flag || this->stop_threads_) {
                return;
            }

            try {
                // 调用设备特定的 CAN 读取逻辑
                device->read_data(*can_operator);
            } catch (const std::exception& e) {
                LOG_ERROR_LOC("CAN thread error for " + device->get_name() + ": " + e.what());
            }
        }
        // 周期停顿，避免占用过多 CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void DeviceManager::stopAllThreads()
{
    LOG_INFO("开始停止所有设备线程...");

    // 1. 停止日志线程
    stopRunningLogThread();

    // 2. 停止云端订阅线程
    stopSubscribeCloudControl();

    // 3. 全局停止标志
    this->stop_threads_.store(true);

    // 4. 设置每个 modbus/CAN 读取线程的停止标志
    for (auto& [id, flag] : this->thread_stop_flags_) {
        flag.store(true);
    }

    // 5. 等待所有设备读取线程退出
    for (auto& thread : this->device_threads_) {
        try {
            if (thread.joinable()) {
                thread.join();
            }
        } catch (...) {
            LOG_ERROR("thread join exception");
        }
    }

    this->device_threads_.clear();

    LOG_INFO("所有设备读取线程已停止");

    // 6. 断开所有 Modbus 客户端连接
    for (auto& [name, client] : this->thread_modbus_clients_) {
        try {
            if (client) {
                client->disconnect();
            }
        } catch (...) {
            LOG_ERROR("disconnect modbus failed:[COM]: " + name);
        }
    }

    // 7. 清理
    this->thread_stop_flags_.clear();
}


// 辅助函数：生成 ISO 8601 格式的时间戳字符串
std::string getCurrentISOTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void DeviceManager::runningLogShowThread() {
    LOG_INFO_LOC("==========================================系统实时监测==========================================");
    
    while (!this->stop_running_log_.load()) {
        try {
            auto ems = EMS::instance();
            
            // 获取当前时间
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            std::tm tm_now = *std::localtime(&time_t_now);
            std::stringstream time_ss;
            time_ss << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
            std::string current_time = time_ss.str();
            
            LOG_INFO_LOC(("[" + current_time + "] " + std::string(50, '-')).c_str());
            
            // 使用共享锁保护读取操作 - EMS 使用专用的 json_rwlock_
            {
                std::shared_lock<std::shared_mutex> lock(ems->json_rwlock_);
                
                LOG_INFO_LOC(("系统运行位置: " + std::to_string(ems->sys_running_pos)).c_str());
                LOG_INFO_LOC(("系统心跳包: " + std::to_string(ems->heartbeat)).c_str());
                // 使用线程安全的 getValue 方法
                LOG_INFO_LOC(("系统开机状态: " + std::to_string(static_cast<int>(ems->getValue<double>("开机", 0)))).c_str());
                
                // 系统状态映射
                std::map<int, std::string> status_map = {
                    {1, "初始化"},
                    {2, "待机"},
                    {3, "开机中"},
                    {4, "充电"},
                    {5, "放电"},
                    {6, "故障"}
                };
                int sys_status = static_cast<int>(ems->getValue<double>("系统状态", 0));
                std::string status_str = status_map.count(sys_status) ? status_map[sys_status] : "未知";
                LOG_INFO_LOC(("系统状态: " + std::to_string(sys_status) + ":" + status_str).c_str());
                
                // 系统运行模式
                std::map<int, std::string> mode_map = {
                    {1, "手动"},
                    {2, "自动"},
                    {3, "定时"},
                    {4, "需求侧响应"},
                    {5, "离网"}
                };
                int run_mode = static_cast<int>(ems->getValue<double>("系统运行模式", 1));
                std::string mode_str = mode_map.count(run_mode) ? mode_map[run_mode] : "未知";
                LOG_INFO_LOC(("系统运行模式: " + std::to_string(run_mode) + ":" + mode_str).c_str());
                
                // 系统告警等级
                int alarm_level = static_cast<int>(ems->getValue<double>("系统告警等级", 0));
                LOG_INFO_LOC(("系统告警等级: " + std::to_string(alarm_level)).c_str());
                
                // 系统功率需求
                if (run_mode == 3) {
                    LOG_INFO_LOC(("系统功率需求: " + std::to_string(ems->weekPlanPower_need) + "kW").c_str());
                } else if (run_mode == 4) {
                    LOG_INFO_LOC(("系统功率需求: " +  std::to_string(ems->demandPower_need) + "kW").c_str());
                }
            }  // EMS锁在此处释放
            
            // 在线设备和离线设备
            std::vector<std::string> online_devices;
            std::vector<std::string> offline_devices;
            for (const auto& device : this->devices_) {
                // LOG_INFO_LOC(device->get_name() + "在线状态: " + std::to_string(device->online_status));
                if (device->online_status) {
                    online_devices.push_back(device->get_name());
                } else {
                    offline_devices.push_back(device->get_name());
                }
            }
            
            std::string online_str = "[";
            for (size_t i = 0; i < online_devices.size(); ++i) {
                online_str += online_devices[i];
                if (i < online_devices.size() - 1) online_str += ", ";
            }
            online_str += "]";
            
            std::string offline_str = "[";
            for (size_t i = 0; i < offline_devices.size(); ++i) {
                offline_str += offline_devices[i];
                if (i < offline_devices.size() - 1) offline_str += ", ";
            }
            offline_str += "]";
            
            LOG_INFO_LOC(("系统在线设备: " + online_str).c_str());
            LOG_INFO_LOC(("系统离线设备: " + offline_str).c_str());
            
            // PCS实时功率 - 使用线程安全访问
            auto pcs_device = this->getDeviceByName("pcs1");
            if (pcs_device) {
                // 使用线程安全的 getValue 方法
                double pcs_power = pcs_device->getValue<double>("模块交流总有功功率", 0.0);
                LOG_INFO_LOC(("PCS实时功率: " + std::to_string(pcs_power) + "kW").c_str());
            }  // PCS锁在此处释放
            
            LOG_INFO_LOC(("[" + current_time + "] " + std::string(50, '*')).c_str());
            
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("运行日志显示线程异常: " + std::string(e.what())).c_str());
        }
        
        // 每5秒输出一次
        for (int i = 0; i < 50; ++i) {

        if (this->stop_running_log_) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (this->stop_running_log_) {
            break;
        }
        }

    LOG_INFO_LOC("运行日志显示线程已停止");
}

void DeviceManager::startRunningLogThread() {
    if (this->running_log_thread_.joinable()) {
        LOG_WARNING_LOC("运行日志显示线程已在运行");
        return;
    }
    
    this->stop_running_log_ = false;
    this->running_log_thread_ = std::thread(&DeviceManager::runningLogShowThread, this);
    LOG_INFO_LOC("运行日志显示线程已启动");
}

void DeviceManager::stopRunningLogThread() {
    this->stop_running_log_ = true;
    
    if (this->running_log_thread_.joinable()) {
        this->running_log_thread_.join();
    }
    
    LOG_INFO_LOC("运行日志显示线程已停止");
}


void DeviceManager::publishDataToRedis()
{
    // 1. 连接到Redis(假设使用默认配置)
    redisContext* redis_conn = redisConnect("127.0.0.1", 6379);
    if (redis_conn == nullptr || redis_conn->err) {
        if (redis_conn) {
            LOG_ERROR_LOC("Redis连接错误: " + std::string(redis_conn->errstr));
            redisFree(redis_conn);
        } else {
            LOG_ERROR_LOC("无法分配Redis上下文");
        }
        return;
    }
    
    // 2. 创建pipeline(在hiredis中通过多个命令然后一次性执行实现)
    std::vector<redisReply*> replies;
    
    // 3. 遍历所有设备
    for (const auto& device : devices_) {
        try {
            // 构建与Python代码结构完全相同的JSON对象
            json data_to_store;
            
            // 基本字段
            data_to_store["name"] = device->get_name();
            
            // data_dict: 使用线程安全的访问方法获取数据副本
            json data_dict_json = json::object();
            {
                // 对于 EMS 设备使用 json_rwlock_,其他设备使用 data_dict_rwlock_
                if (device->get_name() == "ems") {
                    auto ems = std::dynamic_pointer_cast<EMS>(device);
                    if (ems) {
                        std::shared_lock<std::shared_mutex> lock(ems->json_rwlock_);
                        
                        for (const auto& pair : device->data_dict_) {
                            const std::string& key = pair.first;
                            const RegisterData& reg_data = pair.second;
                            
                            json reg_json = {
                                {"value", reg_data.value},
                                {"unit", reg_data.unit}
                            };
                            data_dict_json[key] = reg_json;
                        }
                    }
                } else {
                    // 其他设备使用基类的 data_dict_rwlock_
                    std::shared_lock<std::shared_mutex> lock(device->data_dict_rwlock_);
                    
                    for (const auto& pair : device->data_dict_) {
                        const std::string& key = pair.first;
                        const RegisterData& reg_data = pair.second;
                        
                        json reg_json = {
                            {"value", reg_data.value},
                            {"unit", reg_data.unit}
                        };
                        data_dict_json[key] = reg_json;
                    }
                }
            }  // 锁在此处释放
            
            data_to_store["data"] = data_dict_json;
            
            // 告警状态
            data_to_store["alarm1_status"] = device->alarm_level1;
            data_to_store["alarm2_status"] = device->alarm_level2;
            data_to_store["alarm3_status"] = device->alarm_level3;
            
            // 时间戳
            data_to_store["timestamp"] = getCurrentISOTimeString();
            
            // 在线状态
            data_to_store["online_status"] = device->online_status.load();
            
            // 特殊设备处理(根据Python代码逻辑)
            std::string dev_name = device->get_name();
            
            // 对于EMS设备,添加定时模式和需求响应模式
            if (dev_name == "ems") {
                auto ems = std::dynamic_pointer_cast<EMS>(device);
                if (ems) {
                    std::shared_lock<std::shared_mutex> lock(ems->json_rwlock_);
                    data_to_store["timingModeSet"] = ems->timingModeSet;
                    data_to_store["demandResponseModeSet"] = ems->demandResponseModeSet;
                }
            }
            
            // 序列化JSON(在锁外执行,避免长时间持有锁)
            std::string serialized = data_to_store.dump();
            
            // 构建Redis命令(SETEX key 20 value)
            std::string key = "device:" + dev_name;
            
            // 将命令添加到pipeline
            redisAppendCommand(redis_conn, "SETEX %s 20 %s", 
                              key.c_str(), serialized.c_str());
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("发布设备数据到Redis异常: " + device->get_name() + ", 错误: " + e.what()).c_str());
        }
    }
    
    // 4. 执行pipeline中的所有命令
    for (size_t i = 0; i < devices_.size(); ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(redis_conn, (void**)&reply) == REDIS_OK) {
            replies.push_back(reply);
        }
    }
    
    // 5. 检查执行结果
    bool all_success = true;
    for (auto reply : replies) {
        if (reply == nullptr || reply->type == REDIS_REPLY_ERROR) {
            all_success = false;
            if (reply) {
                LOG_ERROR_LOC("Redis命令执行错误: " + std::string(reply->str));
            }
        }
        freeReplyObject(reply);
    }
    
    if (!all_success) {
        LOG_WARNING_LOC("发布 " + std::to_string(devices_.size()) + " 个设备数据到Redis失败");
    }
    
    // 6. 清理连接
    redisFree(redis_conn);
}


// 启动云端控制订阅
void DeviceManager::startSubscribeCloudControl() {
    if (cloud_control_thread_.joinable()) {
        LOG_WARNING_LOC("云端控制订阅线程已在运行");
        return;
    }
    
    stop_cloud_control_ = false;
    cloud_control_thread_ = std::thread(&DeviceManager::cloudControlSubscribeThread, this);
    LOG_INFO_LOC("云端控制订阅线程已启动");
}

// 停止云端控制订阅
void DeviceManager::stopSubscribeCloudControl() {
    // stop_cloud_control_ = true;
    
    // // 关闭订阅连接
    // if (redis_subscriber_) {
    //     redis_subscriber_->unsubscribe();
    //     redis_subscriber_.reset();
    // }
    
    // if (redis_sub_client_) {
    //     redis_sub_client_.reset();
    // }
    
    // if (cloud_control_thread_.joinable()) {
    //     cloud_control_thread_.join();
    // }

    stop_cloud_control_ = true;
    // 强制关闭 redis 连接，使阻塞的 consume 返回
    if (redis_subscriber_) {
        try {
            redis_subscriber_->unsubscribe();
        } catch (...) {}
        redis_subscriber_.reset();
    }
    if (redis_sub_client_) {
        redis_sub_client_.reset();
    }
    if (cloud_control_thread_.joinable()) {
        cloud_control_thread_.join();
    }
    
    LOG_INFO_LOC("云端控制订阅线程已停止");
}

// 云端控制订阅线程函数
// void DeviceManager::cloudControlSubscribeThread() {
//     try {
//         // 创建Redis客户端（专门用于订阅）
//         redis_sub_client_ = std::make_unique<sw::redis::Redis>("tcp://127.0.0.1:6379");
        
//         // 创建订阅者
//         redis_subscriber_ = std::make_unique<sw::redis::Subscriber>(redis_sub_client_->subscriber());
        
//         // 设置消息回调
//         redis_subscriber_->on_message([this](const std::string& channel, const std::string& msg) {
//             this->handleControlMessage(channel, msg);
//         });
        
//         // 订阅控制频道（与Python端保持一致）
//         std::string control_channel = "cloud/action/xyc2026002/control";
//         redis_subscriber_->subscribe(control_channel);
//         LOG_INFO_LOC(("开始订阅Redis频道: " + control_channel).c_str());
        
//         // 循环消费消息
//         while (!stop_cloud_control_) {
//             try {
//                 redis_subscriber_->consume();  // 阻塞等待消息
//             } catch (const sw::redis::Error& e) {
//                 if (stop_cloud_control_) {
//                     // 如果是主动停止导致的异常，这是预期的行为，静默退出循环
//                     LOG_INFO_LOC("云端控制订阅线程因主动停止而退出。");
//                     break;
//                 } else {
//                     // 非预期错误，记录日志后可选择短暂休息后重试
//                     LOG_ERROR_LOC(("Redis订阅消费错误: " + std::string(e.what())).c_str());
//                     std::this_thread::sleep_for(std::chrono::seconds(1));
//                 }
//             }
//         }
//     } catch (const std::exception& e) {
//         LOG_ERROR_LOC(("云端控制订阅线程异常: " + std::string(e.what())).c_str());
//     }
// }


void DeviceManager::cloudControlSubscribeThread() {
    try {
        // 配置超时时间
        sw::redis::ConnectionOptions opts;
        opts.host = "127.0.0.1";
        opts.port = 6379;
        opts.socket_timeout = std::chrono::milliseconds(1000); // 1秒超时

        redis_sub_client_ = std::make_unique<sw::redis::Redis>(opts);
        redis_subscriber_ = std::make_unique<sw::redis::Subscriber>(redis_sub_client_->subscriber());
        
        // 设置消息回调
        redis_subscriber_->on_message([this](const std::string& channel, const std::string& msg) {
            this->handleControlMessage(channel, msg);
        });
        
        // 订阅控制频道（与Python端保持一致）
        std::string control_channel = "cloud/action/xyc2026002/control";
        redis_subscriber_->subscribe(control_channel);
        LOG_INFO_LOC(("开始订阅Redis频道: " + control_channel).c_str());

        while (!stop_cloud_control_) {
            try {
                redis_subscriber_->consume(); 
            } catch (const sw::redis::TimeoutError& e) {
                // 超时属于正常现象，继续循环检查 stop_cloud_control_
                continue; 
            } catch (const sw::redis::Error& e) {
                if (stop_cloud_control_) break;
                LOG_ERROR_LOC("Redis连接错误，尝试重连...");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("订阅线程崩溃: " + std::string(e.what()));
    }
}




// 处理控制消息
void DeviceManager::handleControlMessage(const std::string& channel, const std::string& message) {
    LOG_INFO_LOC(("收到云端控制消息，频道: " + channel).c_str());
    LOG_INFO_LOC(("消息内容: " + message).c_str());
    
    try {
        // 解析JSON消息
        auto json_msg = nlohmann::json::parse(message);
        
        // 验证 SN 和 PROJECT_CODE（与 Python 端保持一致）
        std::string expected_sn = "xyc2026002";
        std::string expected_code = "15kW南网户储项目1";
        
        if (json_msg.contains("sn") && json_msg["sn"] != expected_sn) {
            LOG_WARNING_LOC(("SN不匹配: 期望=" + expected_sn + ", 实际=" + json_msg["sn"].get<std::string>()).c_str());
            return;
        }
        
        if (json_msg.contains("code") && json_msg["code"] != expected_code) {
            LOG_WARNING_LOC(("PROJECT_CODE不匹配: 期望=" + expected_code + ", 实际=" + json_msg["code"].get<std::string>()).c_str());
            return;
        }
        
        // 将消息存储到Redis（设置5分钟过期时间，与Python一致）
        try {
            redisContext* redis_conn = redisConnect("127.0.0.1", 6379);
            if (redis_conn && !redis_conn->err) {
                std::string redis_key = "mqtt:" + channel;
                redisReply* reply = (redisReply*)redisCommand(redis_conn, 
                    "SETEX %s 300 %s", 
                    redis_key.c_str(), 
                    message.c_str());
                
                if (reply) {
                    freeReplyObject(reply);
                    LOG_INFO_LOC(("消息已存储到Redis: " + redis_key).c_str());
                }
                redisFree(redis_conn);
            }
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("存储消息到Redis失败: " + std::string(e.what())).c_str());
        }
        
        // 如果有设置回调函数，则调用
        if (control_message_callback_) {
            control_message_callback_(channel, message);
        } else {
            // 默认处理逻辑：根据消息内容执行设备控制
            if (json_msg.contains("device") && json_msg.contains("command")) {
                std::string device_name = json_msg["device"];
                std::string command = json_msg["command"];
                
                auto device = getDeviceByName(device_name);
                if (device) {
                    LOG_INFO_LOC(("执行设备控制: " + device_name + " -> " + command).c_str());
                    
                    // 示例：如果是PCS设备，可以设置功率
                    if (device_name == "pcs1" && json_msg.contains("power")) {
                        double power = json_msg["power"];
                        LOG_INFO_LOC(("设置PCS功率: " + std::to_string(power) + "kW").c_str());
                        // TODO: 实际应该调用PCS的控制接口
                        // pcs_->set_power(power);
                    }
                    
                    // 示例：如果是EMS设备，可以设置运行模式
                    if (device_name == "ems") {
                        if (json_msg.contains("mode")) {
                            int mode = json_msg["mode"];
                            LOG_INFO_LOC(("设置EMS运行模式: " + std::to_string(mode)).c_str());
                            // TODO: 实际应该调用EMS的控制接口
                            // ems_->set_run_mode(mode);
                        }
                    }
                } else {
                    LOG_WARNING_LOC(("设备不存在: " + device_name).c_str());
                }
            }
            
            // 处理通用控制命令（cmd_id 和 value 格式，类似 Python 的 mqtt_controller）
            if (json_msg.contains("cmd_id") && json_msg.contains("value")) {
                std::string cmd_id = json_msg["cmd_id"];
                auto value = json_msg["value"];
                
                LOG_INFO_LOC(("收到通用控制命令: cmd_id=" + cmd_id + ", value=" + value.dump()).c_str());
                
                // TODO: 根据 cmd_id 执行相应的控制操作
                // 这里可以集成到现有的命令系统中
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR_LOC(("JSON解析错误: " + std::string(e.what())).c_str());
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("处理控制消息异常: " + std::string(e.what())).c_str());
    }
}

// ═══════════════════════════════════════════════════════════════
// Modbus TCP 服务器 (FC04 所有设备只读, FC03 EMS可读写)
// ═══════════════════════════════════════════════════════════════

// ── 工具：算一个变量的寄存器数量 ──
static int reg_count_of(const std::string& datatype) {
    return (datatype.find("32") != std::string::npos) ? 2 : 1;
}

// ── 工具：真实值 → modbus寄存器 (大端序) ──
static void value_to_regs(double real, double mag, uint16_t offset,
                          const std::string& dt, uint16_t* out, int& cnt) {
    double scaled = (real - static_cast<double>(offset)) * mag;
    cnt = reg_count_of(dt);
    if (cnt >= 2) {
        int32_t v = static_cast<int32_t>(std::round(scaled));
        out[0] = static_cast<uint16_t>((v >> 16) & 0xFFFF);
        out[1] = static_cast<uint16_t>(v & 0xFFFF);
    } else {
        out[0] = static_cast<uint16_t>(static_cast<int16_t>(std::round(scaled)));
    }
}

// ── 工具：modbus寄存器 → 真实值 ──
static double regs_to_value(const uint16_t* regs, int cnt,
                            double mag, uint16_t offset) {
    int32_t raw;
    if (cnt >= 2)
        raw = (static_cast<int32_t>(regs[0]) << 16) | regs[1];
    else
        raw = static_cast<int16_t>(regs[0]);
    return static_cast<double>(raw) / mag + static_cast<double>(offset);
}

// ── 初始化：分配 FC04 地址、建立 FC03 映射、创建数据区 ──
void DeviceManager::initModbusTcpServer(const std::string& ip, int port) {
    this->fc04_offsets_.clear();
    this->fc03_map_.clear();

    // --- FC04: 遍历非EMS设备，分配地址（支持自定义起始地址，未设置则按顺序分配） ---
    uint16_t cursor = 0;
    for (const auto& dev : devices_) {
        if (!dev || dev->get_name() == "ems") continue;

        const auto& keys = dev->dev_data_keys_;
        if (keys.empty()) continue;

        // 自定义起始地址优先，否则使用自动递增的 cursor
        auto custom_it = this->fc04_start_addrs_.find(dev->get_name());
        uint16_t start = (custom_it != this->fc04_start_addrs_.end())
                             ? custom_it->second
                             : cursor;

        uint16_t cnt = 0;
        {
            std::shared_lock<std::shared_mutex> lk(dev->data_dict_rwlock_);
            const auto& dict = dev->data_dict_;
            for (const auto& k : keys) {
                auto it = dict.find(k);
                if (it != dict.end())
                    cnt += reg_count_of(it->second.datatype);
            }
        }
        if (cnt == 0) continue;

        // +1 寄存器用于 online_status
        cnt += 1;

        // 冲突检测：新范围 [start, start+cnt-1] 与已分配范围是否重叠
        uint16_t end = start + cnt - 1;
        bool conflict = false;
        for (const auto& kv : this->fc04_offsets_) {
            uint16_t a = kv.second.first;
            uint16_t b = kv.second.first + kv.second.second - 1;
            if (!(end < a || start > b)) {
                LOG_ERROR_LOC(("FC04 [" + dev->get_name() + "] addr " +
                               std::to_string(start) + "~" + std::to_string(end) +
                               " 与 [" + kv.first + "] addr " +
                               std::to_string(a) + "~" + std::to_string(b) + " 重叠，跳过").c_str());
                conflict = true;
                break;
            }
        }
        if (conflict) continue;

        this->fc04_offsets_[dev->get_name()] = {start, cnt};

        // 未自定义的设备才推进顺序 cursor，自定义设备不影响自动分配
        if (custom_it == this->fc04_start_addrs_.end()) {
            cursor = start + cnt;
        }

        LOG_INFO_LOC(("FC04 [" + dev->get_name() + "] addr " +
                      std::to_string(start) + "~" + std::to_string(end) +
                      " (" + std::to_string(cnt) + " regs)").c_str());
    }

    // total_input = max(end of all allocated ranges)，预留间隙全部纳入
    uint16_t total_input = 1;
    for (const auto& kv : this->fc04_offsets_) {
        uint16_t end = kv.second.first + kv.second.second;
        if (end > total_input) total_input = end;
    }

    // --- FC03: 从 ems_->tcp_cmd 读取可读写变量 ---
    if (ems_) {
        std::shared_lock<std::shared_mutex> lk(ems_->json_rwlock_);
        for (auto& item : ems_->tcp_cmd.items()) {
            const std::string& k = item.key();
            const json& v = item.value();
            if (!v.contains("tcp_addr")) continue;

            uint16_t addr = v["tcp_addr"].get<uint16_t>();
            Fc03Mapping m;
            m.key      = k;
            m.mag      = v.value("mag", 1.0);
            m.offset   = v.value("offset", 0);
            m.datatype = v.value("datatype", "INT16");
            m.reg_count = reg_count_of(m.datatype);
            m.last_val[0] = m.last_val[1] = 0;

            // 用 data_dict 当前值初始化 last_val
            auto dit = ems_->data_dict_.find(k);
            if (dit != ems_->data_dict_.end()) {
                int dummy;
                uint16_t out[2];
                value_to_regs(dit->second.value, m.mag, m.offset, m.datatype, out, dummy);
                m.last_val[0] = out[0];
                if (m.reg_count > 1) m.last_val[1] = out[1];
            }
            this->fc03_map_[addr] = m;
        }
    }

    // 计算 total_holding = max(fc03_end, timer_block_end, demand_block_end)
    uint16_t total_holding = 1;
    if (!this->fc03_map_.empty()) {
        auto last = this->fc03_map_.rbegin();
        total_holding = last->second.reg_count > 1
                            ? last->first + 2
                            : last->first + 1;
    }

    // 确保 total_holding 能覆盖定时模式块和需求响应模式块
    uint16_t timer_block_end = timer_block_start_addr_ + MAX_TIMER_ENTRIES * TIMER_ENTRY_REGS;
    uint16_t demand_block_end = demand_block_start_addr_ + MAX_DEMAND_ENTRIES * DEMAND_ENTRY_REGS;
    if (timer_block_end > total_holding)    total_holding = timer_block_end;
    if (demand_block_end > total_holding)   total_holding = demand_block_end;

    // 初始化 last 缓存
    last_timer_block_.assign(MAX_TIMER_ENTRIES * TIMER_ENTRY_REGS, 0);
    last_demand_block_.assign(MAX_DEMAND_ENTRIES * DEMAND_ENTRY_REGS, 0);

    // 创建 ModbusServer 并分配数据区
    this->modbus_tcp_server_ = std::make_unique<ModbusServer>(ip, std::to_string(port), 10);
    this->modbus_tcp_server_->init_data_area(0, 0, total_holding, total_input);

    LOG_INFO_LOC(("ModbusTCP server初始化: total_holding=" + std::to_string(total_holding) +
                  ", total_input=" + std::to_string(total_input) +
                  ", timer_block=" + std::to_string(timer_block_start_addr_) +
                  "~" + std::to_string(timer_block_end - 1) +
                  ", demand_block=" + std::to_string(demand_block_start_addr_) +
                  "~" + std::to_string(demand_block_end - 1)).c_str());
}

// ── 启动 ──
void DeviceManager::startModbusTcpServer() {
    std::string ip = "0.0.0.0";
    int port = 1026;
    // 设置每个设备的 FC04 ModbusTcp服务器起始地址
    setDeviceFc04StartAddr("pcs1", 0);
    setDeviceFc04StartAddr("bms_uhome", 300);
    setDeviceFc04StartAddr("dtsd3366", 600);
    setDeviceFc04StartAddr("ac_wea1610", 700);
    setDeviceFc04StartAddr("dehumidifier", 750);

    initModbusTcpServer(ip, port);

    // 先写一次初始值
    syncAllFc04();
    syncAllFc03();
    syncTimerBlock();
    syncDemandBlock();

    if (!this->modbus_tcp_server_->start()) {
        LOG_ERROR_LOC("Modbus TCP server启动失败！");
        return;
    }

    this->modbus_sync_running_ = true;
    this->modbus_sync_thread_ = std::thread(&DeviceManager::modbusSyncLoop, this);
    LOG_INFO_LOC("Modbus TCP server启动成功:" + ip + ":" + std::to_string(port));
}

// ── 停止 ──
void DeviceManager::stopModbusTcpServer() {
    modbus_sync_running_ = false;
    if (this->modbus_sync_thread_.joinable()) this->modbus_sync_thread_.join();
    if (this->modbus_tcp_server_) this->modbus_tcp_server_->stop();
    LOG_INFO_LOC("Modbus TCP server stopped");
}

// ── 后台同步线程 (≈1秒) ──
void DeviceManager::modbusSyncLoop() {
    while (this->modbus_sync_running_) {
        for (int i = 0; i < 10 && this->modbus_sync_running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!this->modbus_sync_running_) break;

        try {
            syncAllFc04();               // 刷新所有设备 → 输入寄存器
            syncAllFc03();               // 双向同步: 检测外部写入→EMS, EMS→保持寄存器
            syncTimerBlock();            // 双向同步: 检测外部写入→EMS, EMS→定时模式块
            syncDemandBlock();           // 双向同步: 检测外部写入→EMS, EMS→需求响应块
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("Modbus服务器同步错误: " + std::string(e.what())).c_str());
        }
    }
}

// ── FC04: 将设备 data_dict 同步到输入寄存器 ──
void DeviceManager::syncAllFc04() {
    if (!this->modbus_tcp_server_) return;

    // 优化：先复制所有设备数据，减少持锁时间
    struct DeviceData {
        std::string name;
        uint16_t start_addr;
        bool online;
        std::vector<std::pair<std::string, RegisterData>> registers;
    };

    std::vector<DeviceData> all_devices_data;

    for (const auto& dev : devices_) {
        if (!dev || dev->get_name() == "ems") continue;

        auto off_it = this->fc04_offsets_.find(dev->get_name());
        if (off_it == this->fc04_offsets_.end()) continue;

        DeviceData dev_data;
        dev_data.name = dev->get_name();
        dev_data.start_addr = off_it->second.first;
        dev_data.online = dev->online_status.load();

        {
            std::shared_lock<std::shared_mutex> lk(dev->data_dict_rwlock_);
            const auto& keys = dev->dev_data_keys_;
            const auto& dict = dev->data_dict_;

            for (const auto& k : keys) {
                auto it = dict.find(k);
                if (it != dict.end()) {
                    dev_data.registers.push_back({k, it->second});
                }
            }
        }  // 设备锁在此处释放

        if (!dev_data.registers.empty()) {
            all_devices_data.push_back(std::move(dev_data));
        }
    }

    // 在锁外更新 Modbus 寄存器
    for (auto& dev_data : all_devices_data) {
        // 首个寄存器：online_status
        this->modbus_tcp_server_->set_input_register(dev_data.start_addr,
                                                       dev_data.online ? 1 : 0);
        uint16_t addr = dev_data.start_addr + 1;

        for (const auto& reg_pair : dev_data.registers) {
            const RegisterData& rd = reg_pair.second;
            uint16_t out[2]; int cnt;
            value_to_regs(rd.value, rd.mag, rd.offset, rd.datatype, out, cnt);

            if (cnt >= 2)
                this->modbus_tcp_server_->set_input_registers(addr, 2, out);
            else
                this->modbus_tcp_server_->set_input_register(addr, out[0]);
            addr += cnt;
        }
    }
}

// ── FC03: 双向同步（检测客户端写入→EMS, EMS→保持寄存器），单趟消除竞态 ──
void DeviceManager::syncAllFc03() {
    if (!this->modbus_tcp_server_ || !ems_) return;
    if (this->fc03_map_.empty()) return;

    // 1. 先读取所有 EMS 当前值（shared_lock）
    std::map<std::string, double> ems_values;
    {
        std::shared_lock<std::shared_mutex> lk(ems_->json_rwlock_);
        for (const auto& pair : this->fc03_map_) {
            auto dit = ems_->data_dict_.find(pair.second.key);
            if (dit != ems_->data_dict_.end())
                ems_values[pair.second.key] = dit->second.value;
        }
    }

    bool server_running = this->modbus_tcp_server_->is_running();

    // 2. 逐寄存器处理：先检测客户端写入，再决定推/拉方向
    struct ClientWrite {
        std::string key;
        double real;
    };
    std::vector<ClientWrite> client_writes;

    for (auto& pair : this->fc03_map_) {
        uint16_t addr = pair.first;
        Fc03Mapping& m = pair.second;

        // 读当前 HR 值
        uint16_t cur[2] = {0, 0};
        if (!this->modbus_tcp_server_->get_holding_register(addr, &cur[0])) continue;
        if (m.reg_count > 1)
            this->modbus_tcp_server_->get_holding_register(addr + 1, &cur[1]);

        // 服务器未启动时禁止客户端写入检测（HR 刚由 init_data_area 清零，
        // last_val 却是从 EMS 初始化的实际值，会误判为客户端写入 0）
        bool changed = server_running &&
                       ((cur[0] != m.last_val[0]) ||
                        (m.reg_count > 1 && cur[1] != m.last_val[1]));

        if (changed) {
            // 客户端写入 → 记录，稍后批量更新 EMS
            double real = regs_to_value(cur, m.reg_count, m.mag, m.offset);
            client_writes.push_back({m.key, real});
            m.last_val[0] = cur[0];
            if (m.reg_count > 1) m.last_val[1] = cur[1];

            LOG_INFO_LOC(("FC03 客户端写入: [" + m.key + "] raw=" +
                          std::to_string(cur[0]) + " real=" + std::to_string(real)).c_str());
        } else {
            // 无外部写入 → EMS 值推送到 HR
            auto vit = ems_values.find(m.key);
            if (vit == ems_values.end()) continue;

            uint16_t out[2]; int cnt;
            value_to_regs(vit->second, m.mag, m.offset, m.datatype, out, cnt);

            // 服务器运行时：写入前再次确认 HR 未被客户端修改（消除 TOCTOU 窗口）
            if (server_running) {
                uint16_t recheck[2] = {0, 0};
                if (!this->modbus_tcp_server_->get_holding_register(addr, &recheck[0])) continue;
                if (m.reg_count > 1)
                    this->modbus_tcp_server_->get_holding_register(addr + 1, &recheck[1]);

                bool race_detected = (recheck[0] != m.last_val[0]) ||
                                     (m.reg_count > 1 && recheck[1] != m.last_val[1]);

                if (race_detected) {
                    // 竞态：在读 EMS 和写入之间客户端修改了 HR → 转为客户端写入处理
                    double real = regs_to_value(recheck, m.reg_count, m.mag, m.offset);
                    client_writes.push_back({m.key, real});
                    m.last_val[0] = recheck[0];
                    if (m.reg_count > 1) m.last_val[1] = recheck[1];

                    LOG_INFO_LOC(("FC03 竞态转客户端写入: [" + m.key + "] raw=" +
                                  std::to_string(recheck[0]) + " real=" + std::to_string(real)).c_str());
                    continue;
                }
            }

            // 安全：写入 EMS 值到 HR
            if (cnt >= 2)
                this->modbus_tcp_server_->set_holding_registers(addr, 2, out);
            else
                this->modbus_tcp_server_->set_holding_register(addr, out[0]);

            // 读回确认实际写入值
            uint16_t confirm[2] = {0, 0};
            this->modbus_tcp_server_->get_holding_register(addr, &confirm[0]);
            if (m.reg_count > 1)
                this->modbus_tcp_server_->get_holding_register(addr + 1, &confirm[1]);

            m.last_val[0] = confirm[0];
            if (m.reg_count > 1) m.last_val[1] = confirm[1];
        }
    }

    // 3. 批量更新 EMS data_dict 并持久化（服务器未启动时跳过，不需要写回）
    if (!client_writes.empty() && server_running) {
        std::unique_lock<std::shared_mutex> lk(ems_->json_rwlock_);
        json data_to_save;
        for (const auto& cw : client_writes) {
            auto dit = ems_->data_dict_.find(cw.key);
            if (dit != ems_->data_dict_.end()) {
                dit->second.value = cw.real;
            }
            data_to_save[cw.key] = cw.real;
        }
        if (!data_to_save.empty()) {
            ems_->write_jsonfile_nolock(data_to_save);
        }
    }
}
//  定时模式块 (timer_block_start_addr_, 默认 100)
//  每条记录 6 个寄存器:
//    [0] startHour   [1] startMinute  [2] endHour
//    [3] endMinute   [4] weekday(bitmask)  [5] power(signed)
// ═══════════════════════════════════════════════════════════════

// 辅助: "08:00" → {hour, minute}
static std::pair<int,int> parse_time_str(const std::string& s) {
    auto pos = s.find(':');
    if (pos == std::string::npos) return {0, 0};
    return {std::stoi(s.substr(0, pos)), std::stoi(s.substr(pos + 1))};
}

// 辅助: weekday 字符串数组 → bitmask (bit0=Monday)
static int weekday_json_to_int(const json& arr) {
    int mask = 0;
    for (size_t i = 0; i < arr.size() && i < 7; ++i) {
        if (arr[i].get<std::string>() == "1") mask |= (1 << i);
    }
    return mask;
}

// 辅助: bitmask → weekday 字符串数组
static json int_to_weekday_json(int mask) {
    json arr = json::array();
    for (int i = 0; i < 7; ++i)
        arr.push_back((mask & (1 << i)) ? "1" : "0");
    return arr;
}

// 辅助: "YYYY-MM-DD HH:MM" → {year, month, day, hour, minute}
static std::tuple<int,int,int,int,int> parse_datetime_str(const std::string& s) {
    if (s.length() < 16) return {1970, 1, 1, 0, 0};
    return {
        std::stoi(s.substr(0, 4)),
        std::stoi(s.substr(5, 2)),
        std::stoi(s.substr(8, 2)),
        std::stoi(s.substr(11, 2)),
        std::stoi(s.substr(14, 2))
    };
}

// 辅助: 年份/月/日/时/分 → "YYYY-MM-DD HH:MM"
static std::string format_datetime(int y, int m, int d, int hh, int mm) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", y, m, d, hh, mm);
    return buf;
}

// ── 定时模式块：双向同步（检测客户端写入→EMS, EMS→保持寄存器），单趟消除竞态 ──
void DeviceManager::syncTimerBlock() {
    if (!this->modbus_tcp_server_ || !ems_) return;

    size_t total = MAX_TIMER_ENTRIES * TIMER_ENTRY_REGS;

    // 1. 读当前 HR 块
    std::vector<uint16_t> cur(total);
    for (size_t i = 0; i < total; ++i) {
        uint16_t v;
        if (!this->modbus_tcp_server_->get_holding_register(
                timer_block_start_addr_ + i, &v)) return;
        cur[i] = v;
    }

    bool client_wrote = (cur != last_timer_block_);
    if (!client_wrote) {
        // 2a. 无外部写入 → 读 EMS，推送到 HR
        json timing;
        {
            std::shared_lock<std::shared_mutex> lk(ems_->json_rwlock_);
            timing = ems_->timingModeSet;
        }

        std::vector<uint16_t> regs(total, 0);
        int idx = 0;
        if (timing.contains("chargeTimeList") && timing["chargeTimeList"].is_array()) {
            for (const auto& entry : timing["chargeTimeList"]) {
                if (idx >= static_cast<int>(total)) break;
                auto [sh, sm] = parse_time_str(entry.value("startTime", "00:00"));
                auto [eh, em] = parse_time_str(entry.value("endTime", "00:00"));
                int wd = 0;
                if (entry.contains("weekday") && entry["weekday"].is_array())
                    wd = weekday_json_to_int(entry["weekday"]);
                int16_t power = static_cast<int16_t>(entry.value("power", 0));

                regs[idx++] = static_cast<uint16_t>(sh);
                regs[idx++] = static_cast<uint16_t>(sm);
                regs[idx++] = static_cast<uint16_t>(eh);
                regs[idx++] = static_cast<uint16_t>(em);
                regs[idx++] = static_cast<uint16_t>(wd);
                regs[idx++] = static_cast<uint16_t>(power);
            }
        }

        // 写入前 recheck HR（消除 TOCTOU 窗口）
        for (size_t i = 0; i < total; ++i) {
            uint16_t v;
            if (!this->modbus_tcp_server_->get_holding_register(
                    timer_block_start_addr_ + i, &v)) return;
            cur[i] = v;
        }

        if (cur != last_timer_block_) {
            // 竞态：在读 EMS 和写入之间客户端修改了 HR → 按客户端写入处理
            client_wrote = true;
        } else if (regs != last_timer_block_) {
            // 安全写入
            for (size_t i = 0; i < total; ++i)
                this->modbus_tcp_server_->set_holding_register(
                    timer_block_start_addr_ + i, regs[i]);

            // 读回确认
            for (size_t i = 0; i < total; ++i) {
                uint16_t v;
                this->modbus_tcp_server_->get_holding_register(
                    timer_block_start_addr_ + i, &v);
                cur[i] = v;
            }
            last_timer_block_ = cur;
            return;
        } else {
            return; // 无变化
        }
    }

    // 2b. 客户端写入 → 解析并更新 EMS
    {
        json charge_list = json::array();
        for (size_t i = 0; i + TIMER_ENTRY_REGS <= cur.size(); i += TIMER_ENTRY_REGS) {
            bool all_zero = true;
            for (int j = 0; j < TIMER_ENTRY_REGS; ++j) {
                if (cur[i + j] != 0) { all_zero = false; break; }
            }
            if (all_zero) continue;

            int sh = cur[i], sm = cur[i + 1], eh = cur[i + 2], em = cur[i + 3];
            int wd = cur[i + 4];
            int16_t power = static_cast<int16_t>(cur[i + 5]);

            char start_buf[6], end_buf[6];
            snprintf(start_buf, sizeof(start_buf), "%02d:%02d", sh, sm);
            snprintf(end_buf,   sizeof(end_buf),   "%02d:%02d", eh, em);

            json entry;
            entry["startTime"] = start_buf;
            entry["endTime"]   = end_buf;
            entry["weekday"]   = int_to_weekday_json(wd);
            entry["power"]     = power;
            charge_list.push_back(entry);
        }

        std::unique_lock<std::shared_mutex> lk(ems_->json_rwlock_);
        ems_->timingModeSet["chargeTimeList"] = charge_list;
        ems_->tcp_timingModeSet = ems_->timingModeSet;
        ems_->write_timerJsonFile(
            json{{"timingModeSet", ems_->timingModeSet}},
            Config::EMS_CONFIG_FILEPATH_JSON);
    }

    last_timer_block_ = cur;
    LOG_INFO_LOC("检测到远程定时模式块写入，已同步");
}

// ── 需求响应模式块：双向同步（检测客户端写入→EMS, EMS→保持寄存器），单趟消除竞态 ──
void DeviceManager::syncDemandBlock() {
    if (!this->modbus_tcp_server_ || !ems_) return;

    size_t total = MAX_DEMAND_ENTRIES * DEMAND_ENTRY_REGS;

    // 1. 读当前 HR 块
    std::vector<uint16_t> cur(total);
    for (size_t i = 0; i < total; ++i) {
        uint16_t v;
        if (!this->modbus_tcp_server_->get_holding_register(
                demand_block_start_addr_ + i, &v)) return;
        cur[i] = v;
    }

    bool client_wrote = (cur != last_demand_block_);
    if (!client_wrote) {
        // 2a. 无外部写入 → 读 EMS，推送到 HR
        json demand;
        {
            std::shared_lock<std::shared_mutex> lk(ems_->json_rwlock_);
            demand = ems_->demandResponseModeSet;
        }

        std::vector<uint16_t> regs(total, 0);
        int idx = 0;
        if (demand.is_array()) {
            for (const auto& entry : demand) {
                if (idx >= static_cast<int>(total)) break;
                auto [sy, smo, sd, sh, smi] =
                    parse_datetime_str(entry.value("startDatetime", "1970-01-01 00:00"));
                auto [ey, emo, ed, eh, emi] =
                    parse_datetime_str(entry.value("endDatetime", "1970-01-01 00:00"));
                int16_t active   = static_cast<int16_t>(entry.value("activePower", 0));
                int16_t reactive = static_cast<int16_t>(entry.value("reactivePower", 0));

                regs[idx++] = static_cast<uint16_t>(sy);
                regs[idx++] = static_cast<uint16_t>(smo);
                regs[idx++] = static_cast<uint16_t>(sd);
                regs[idx++] = static_cast<uint16_t>(sh);
                regs[idx++] = static_cast<uint16_t>(smi);
                regs[idx++] = static_cast<uint16_t>(ey);
                regs[idx++] = static_cast<uint16_t>(emo);
                regs[idx++] = static_cast<uint16_t>(ed);
                regs[idx++] = static_cast<uint16_t>(eh);
                regs[idx++] = static_cast<uint16_t>(emi);
                regs[idx++] = static_cast<uint16_t>(active);
                regs[idx++] = static_cast<uint16_t>(reactive);
            }
        }

        // 写入前 recheck HR（消除 TOCTOU 窗口）
        for (size_t i = 0; i < total; ++i) {
            uint16_t v;
            if (!this->modbus_tcp_server_->get_holding_register(
                    demand_block_start_addr_ + i, &v)) return;
            cur[i] = v;
        }

        if (cur != last_demand_block_) {
            client_wrote = true;
        } else if (regs != last_demand_block_) {
            for (size_t i = 0; i < total; ++i)
                this->modbus_tcp_server_->set_holding_register(
                    demand_block_start_addr_ + i, regs[i]);

            for (size_t i = 0; i < total; ++i) {
                uint16_t v;
                this->modbus_tcp_server_->get_holding_register(
                    demand_block_start_addr_ + i, &v);
                cur[i] = v;
            }
            last_demand_block_ = cur;
            return;
        } else {
            return;
        }
    }

    // 2b. 客户端写入 → 解析并更新 EMS
    {
        json demand_list = json::array();
        for (size_t i = 0; i + DEMAND_ENTRY_REGS <= cur.size(); i += DEMAND_ENTRY_REGS) {
            bool all_zero = true;
            for (int j = 0; j < DEMAND_ENTRY_REGS; ++j) {
                if (cur[i + j] != 0) { all_zero = false; break; }
            }
            if (all_zero) continue;

            std::string start_dt = format_datetime(cur[i], cur[i+1], cur[i+2], cur[i+3], cur[i+4]);
            std::string end_dt   = format_datetime(cur[i+5], cur[i+6], cur[i+7], cur[i+8], cur[i+9]);
            int16_t active   = static_cast<int16_t>(cur[i + 10]);
            int16_t reactive = static_cast<int16_t>(cur[i + 11]);

            json entry;
            entry["startDatetime"] = start_dt;
            entry["endDatetime"]   = end_dt;
            entry["activePower"]   = active;
            entry["reactivePower"] = reactive;
            demand_list.push_back(entry);
        }

        std::unique_lock<std::shared_mutex> lk(ems_->json_rwlock_);
        ems_->demandResponseModeSet = demand_list;
        ems_->tcp_demandResponseModeSet = demand_list;
        ems_->write_timerJsonFile(
            json{{"demandResponseModeSet", ems_->demandResponseModeSet}},
            Config::EMS_CONFIG_FILEPATH_JSON);
    }

    last_demand_block_ = cur;
    LOG_INFO_LOC("检测到远程需求响应模式块写入，已同步");
}
