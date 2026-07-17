#include "devicemanager.h"
#include <iostream>
#include <cmath>
#include "config.h"
#include "utils.h"
#include "canoperator.h"
#include "modbusclient.h"
#include "sqlcpp.h"
#include <hiredis/hiredis.h>
#include "log.h"
#include "control/qtcontroller.h"
#include <thread>
#include <map>
#include <chrono>
#include <iomanip>
#include <functional>  // 用于 std::function
#include <algorithm>
#include <unordered_set>
#include <fstream>

DeviceManager::DeviceManager() {
    // 初始化所有设备实例
    this->ems_ = EMS::instance();
    this->pcs15am_ = std::make_shared<Pcs>("pcs1", 0, 1);
    // 创建高特BMS设备（假设使用串口8，为Modbus TCP，Modbus从站地址为1）
    this->gt_bms_ = std::make_shared<GtBms>("gtbms485", 8, 1);  
    this->iomodule_ = std::make_shared<IOModule>("board_8di8do", 1, 20);
    this->dtsd3366_ = std::make_shared<ACMeter_3366>("dtsd3366", 2, 1);
    

    // this->ac_hengdu_ = std::make_shared<AcHengdu>("air_condition", 3, 1);
    // this->dg_hgm6100_ = std::make_shared<DgHgm6100n>("dg_hgm6100n", 5, 1);
    // this->iomodule_ = std::make_shared<IOModule>("board_8di8do", 7, 20);
    // this->chargers_ = std::make_shared<InfyCharger>("chargers", 17, 1);
    // this->chargers_->init_config(Config::INFY_CHARGER_COMMUNICATION_FILEPATH);
    
    
    
    this->devices_ = {this->ems_, this->pcs15am_, this->dtsd3366_,
                    this->iomodule_, this->gt_bms_, 
                      }; 
    
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
            this->com_dev_map[com_num].push_back(device); 
        } else if (Config::CAN_INTERFACES.find(com_num) != Config::CAN_INTERFACES.end()) {
            this->can_dev_map[com_num].push_back(device);
        } else {
            LOG_ERROR_LOC("设备串口或CAN接口不存在: " + device->get_name());
        }
    }
    // 2. 遍历串口 (Modbus) 端口，为每个端口启动一个读取线程
    for (auto& pair : this->com_dev_map){
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
                LOG_INFO_LOC(("创建ModbusClient for TCP: " + it->second).c_str());
                auto [ip,port] = Utils::splitIpPort(it->second);
                modbus_client = std::make_shared<ModbusClient>(ip, port);
            }
            
            // 尝试连接，如果失败则记录警告但继续运行
            bool connected = modbus_client->connect();
            if (!connected){
                LOG_WARNING_LOC(("ModbusClient connection 失败 for " + it->second + ", 该设备将被跳过").c_str());
                LOG_WARNING_LOC("继续处理其他设备...");
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
            LOG_ERROR_LOC("创建读取线程错误: COM " + 
                         std::to_string(static_cast<int>(pair.first)));
        }
       
    }

    // 3. 遍历 CAN 接口，为每个接口启动一个读取线程
    for (auto& pair : this->can_dev_map) {
        auto it = Config::CAN_INTERFACES.find(pair.first);
        if (it == Config::CAN_INTERFACES.end()) {
            LOG_ERROR_LOC("找不到CAN接口:  " + 
                         std::to_string(static_cast<int>(pair.first)));

            continue;
        }

        auto can_operator = std::make_shared<CanOperator>(it->second, 0);
        const bool connected = can_operator->connect();
        if (!connected) {
            LOG_WARNING_LOC(("CanOperator连接错误: " + it->second +
                             ", 该设备组将被跳过").c_str());
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
        ("Modbus读取线程启动:COM" +
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
                ("Modbus读取线程错误:COM" +
                std::to_string(static_cast<int>(com))).c_str());

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));

            continue;
        }

        // 遍历当前串口下所有设备
        for (auto& device : this->com_dev_map[com])
        {
            // 再次检查停止状态
            auto stop_it =
                this->thread_stop_flags_.find(thread_id);

            if (this->stop_threads_.load() ||
                stop_it == this->thread_stop_flags_.end() ||
                stop_it->second.load())
            {
                LOG_INFO_LOC(
                    ("Modbus读取线程退出:COM" +
                    std::to_string(static_cast<int>(com))).c_str());

                return;
            }

            // 空设备保护
            if (!device) {

                LOG_ERROR_LOC("设备为空");

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
        ("Modbus读取线程停止:COM" +
        std::to_string(static_cast<int>(com))).c_str());
}


/**
 * @brief CAN 设备的线程主循环
 * 轮询该接口下挂载的所有 CAN 设备
 */
void DeviceManager::readCanDeviceThreadWithStopFlag(uint8_t com,
    std::shared_ptr<CanOperator> can_operator, int thread_id)
{
    // 获取当前线程的停止标志引用
    auto& stop_flag = this->thread_stop_flags_[thread_id];

    LOG_INFO_LOC(("CAN读取线程启动:COM" + std::to_string(static_cast<int>(com))).c_str());

    int consecutive_errors = 0;         // 连续错误计数
    static constexpr int MAX_CONSECUTIVE_ERRORS = 10;

    while (!stop_flag && !this->stop_threads_) {
        // ── 自动重连：如果 CAN 接口断开，尝试重连 ──
        if (!can_operator->is_connected()) {
            LOG_WARNING_LOC(("CAN interface " + can_operator->get_interface_name() +
                             " disconnected, attempting reconnect...").c_str());
            if (can_operator->connect()) {
                LOG_INFO_LOC(("CAN interface " + can_operator->get_interface_name() +
                              " reconnected successfully").c_str());
                consecutive_errors = 0;
            } else {
                // 重连失败，等待后重试
                std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                continue;
            }
        }

        // 依次读取该接口下的每个设备
        for (auto& device : this->can_dev_map[com]) {
            if (stop_flag || this->stop_threads_) {
                return;
            }

            try {
                // 调用设备特定的 CAN 读取逻辑
                device->read_data(*can_operator);
                consecutive_errors = 0;  // 成功则重置错误计数
            } catch (const std::exception& e) {
                LOG_ERROR_LOC("CAN thread error for " + device->get_name() + ": " + e.what());
                consecutive_errors++;

                // 连续错误超过阈值，触发重连
                if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
                    LOG_WARNING_LOC(("Too many CAN errors (" +
                                     std::to_string(consecutive_errors) +
                                     "), forcing reconnect...").c_str());
                    can_operator->disconnect();
                    consecutive_errors = 0;
                    break;  // 跳出设备循环，下一轮迭代会触发重连
                }
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

    // 2. 停止数据库定时插入线程
    stopDbInserterThread();

    // 3. 停止云端订阅线程
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
static std::string getCurrentISOTimeString() {
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
            std::string current_time = getCurrentISOTimeString();   
            
            LOG_INFO_LOC(("[" + current_time + "] " + std::string(50, '-')).c_str());
            
            static const std::map<int, std::string> status_map = {
                {1, "初始化"},
                {2, "待机"},
                {3, "开机中"},
                {4, "充电"},
                {5, "放电"},
                {6, "故障"}
            };
            static const std::map<int, std::string> mode_map = {
                {1, "手动"},
                {2, "自动"},
                {3, "定时"},
                {4, "需求侧响应"},
                {5, "离网"}
            };

            int sys_running_pos;
            int heartbeat;
            int power_on;
            int sys_status;
            int run_mode;
            int alarm_level;
            double weekPlanPower_need = 0;
            double demandPower_need = 0;

            power_on = static_cast<int>(ems->getValue<double>("开机", 0));
            sys_status = static_cast<int>(ems->getValue<double>("系统状态", 0));
            run_mode = static_cast<int>(ems->getValue<double>("系统运行模式", 1));
            alarm_level = static_cast<int>(ems->getValue<double>("系统告警等级", 0));

            sys_running_pos = ems->sys_running_pos.load();
            heartbeat = ems->heartbeat.load();
            {
                std::shared_lock<std::shared_mutex> lock(ems->json_rwlock_);
                if (run_mode == RunMode::TIMER) {
                    weekPlanPower_need = ems->weekPlanPower_need;
                } else if (run_mode == RunMode::DEMAND_RESPONSE) {
                    demandPower_need = ems->demandPower_need;
                }
            }

            LOG_INFO_LOC(("系统运行位置: " + std::to_string(sys_running_pos)).c_str());
            LOG_INFO_LOC(("系统心跳包: " + std::to_string(heartbeat)).c_str());
            LOG_INFO_LOC(("系统开机状态: " + std::to_string(power_on)).c_str());

            std::string status_str = status_map.count(sys_status) ? status_map.at(sys_status) : "未知";
            LOG_INFO_LOC(("系统状态: " + std::to_string(sys_status) + ":" + status_str).c_str());

            std::string mode_str = mode_map.count(run_mode) ? mode_map.at(run_mode) : "未知";
            LOG_INFO_LOC(("系统运行模式: " + std::to_string(run_mode) + ":" + mode_str).c_str());

            LOG_INFO_LOC(("系统告警等级: " + std::to_string(alarm_level)).c_str());

            if (run_mode == RunMode::TIMER) {
                LOG_INFO_LOC(("系统功率需求: " + std::to_string(weekPlanPower_need) + "kW").c_str());
            } else if (run_mode == RunMode::DEMAND_RESPONSE) {
                LOG_INFO_LOC(("系统功率需求: " + std::to_string(demandPower_need) + "kW").c_str());
            }
            
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
            auto pcs15am_device = this->getDeviceByName("pcs1");
            if (pcs15am_device) {
                // 使用线程安全的 getValue 方法
                double pcs15am_power = pcs15am_device->getValue<double>("模块交流总有功功率", 0.0);
                LOG_INFO_LOC(("PCS实时功率: " + std::to_string(pcs15am_power) + "kW").c_str());
            }  // PCS锁在此处释放
            
            LOG_INFO_LOC(("[" + current_time + "] " + std::string(50, '*')).c_str());
            
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("运行日志显示线程异常: " + std::string(e.what())).c_str());
        }
        
        // 每10秒输出一次
        for (int i = 0; i < 100; ++i) {
            if (this->stop_running_log_)  break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (this->stop_running_log_) break;
        
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

// ═══════════════════════════════════════════════════════════════
// 数据库定时插入（从Redis获取设备数据，避免对 data_dict_ 加锁）
// ═══════════════════════════════════════════════════════════════

void DeviceManager::saveDeviceDataFromRedis() {
    // 1. 连接Redis
    redisContext* redis_conn = redisConnect("127.0.0.1", 6379);
    if (redis_conn == nullptr || redis_conn->err) {
        if (redis_conn) {
            LOG_ERROR_LOC("DB插入线程 Redis连接错误: " + std::string(redis_conn->errstr));
            redisFree(redis_conn);
        } else {
            LOG_ERROR_LOC("DB插入线程 无法分配Redis上下文");
        }
        return;
    }

    // 设置超时
    struct timeval timeout = {2, 0};  // 2秒
    redisSetTimeout(redis_conn, timeout);

    int total_inserted = 0;
    int skipped_offline = 0;
    int skipped_missing = 0;

    // 2. 遍历所有设备，从Redis获取数据
    for (const auto& device : devices_) {
        if (!device) continue;

        const std::string dev_name = device->get_name();
        std::string redis_key = "device:" + dev_name;

        // 从Redis获取JSON数据
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(redis_conn, "GET %s", redis_key.c_str()));

        if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
            if (reply) freeReplyObject(reply);
            skipped_missing++;
            continue;
        }

        std::string json_str(reply->str, reply->len);
        freeReplyObject(reply);

        // 3. 解析JSON
        try {
            auto data_json = nlohmann::json::parse(json_str);

            // 检查在线状态
            bool online = data_json.value("online_status", false);
            if (!online) {
                skipped_offline++;
                continue;
            }

            // 提取 data 字段
            if (!data_json.contains("data") || !data_json["data"].is_object()) {
                skipped_missing++;
                continue;
            }

            const auto& data_obj = data_json["data"];

            // 4. 构建 RegisterData map（从Redis JSON重建）
            std::unordered_map<std::string, RegisterData> register_data;
            for (auto it = data_obj.begin(); it != data_obj.end(); ++it) {
                const std::string& key = it.key();
                const auto& val = it.value();

                RegisterData rd;
                rd.value    = val.value("value", 0.0);
                rd.mag      = val.value("mag", 1.0);
                rd.offset   = val.value("offset", 0);
                rd.datatype = val.value("datatype", "INT16");
                rd.unit     = val.value("unit", "");

                register_data[key] = rd;
            }

            if (register_data.empty()) {
                skipped_missing++;
                continue;
            }

            // 5. 插入数据库（表名使用设备名小写）
            std::string table_name = dev_name;
            std::transform(table_name.begin(), table_name.end(),
                           table_name.begin(), ::tolower);

            if (SqlCpp::getInstance().insertDeviceDataFromRegister(
                    table_name, register_data, online)) {
                total_inserted++;
            }

        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR_LOC(("DB插入 解析JSON失败 [" + dev_name + "]: " + e.what()).c_str());
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("DB插入 异常 [" + dev_name + "]: " + e.what()).c_str());
        }
    }

    // 6. 清理Redis连接
    redisFree(redis_conn);

    LOG_INFO_LOC(("数据库插入完成: 成功=" + std::to_string(total_inserted) +
                  ", 离线跳过=" + std::to_string(skipped_offline) +
                  ", 无数据跳过=" + std::to_string(skipped_missing)).c_str());
}

void DeviceManager::dbInserterThreadLoop(int save_period_seconds) {
    LOG_INFO_LOC(("数据库定时插入线程启动，保存周期=" +
                  std::to_string(save_period_seconds) + "秒").c_str());

    // 等待首个周期再开始（避免启动时集中写入）
    for (int i = 0; i < save_period_seconds && !stop_db_inserter_; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    while (!stop_db_inserter_.load()) {
        try {
            saveDeviceDataFromRedis();
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("数据库插入线程异常: " + std::string(e.what())).c_str());
        }

        // 按周期等待，每秒检查停止标志以便快速退出
        for (int i = 0; i < save_period_seconds && !stop_db_inserter_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    LOG_INFO_LOC("数据库定时插入线程已停止");
}

void DeviceManager::startDbInserterThread(int save_period_seconds) {
    if (db_inserter_thread_.joinable()) {
        LOG_WARNING_LOC("数据库定时插入线程已在运行");
        return;
    }

    // 确保数据库已初始化
    if (!SqlCpp::getInstance().isInitialized()) {
        LOG_WARNING_LOC("数据库未初始化，尝试初始化...");
        if (!SqlCpp::getInstance().initialize()) {
            LOG_ERROR_LOC("数据库初始化失败，数据库插入线程未启动");
            return;
        }
    }

    stop_db_inserter_ = false;
    db_inserter_thread_ = std::thread(
        &DeviceManager::dbInserterThreadLoop, this, save_period_seconds);
    LOG_INFO_LOC(("数据库定时插入线程已启动，周期=" +
                  std::to_string(save_period_seconds) + "秒").c_str());
}

void DeviceManager::stopDbInserterThread() {
    stop_db_inserter_ = true;

    if (db_inserter_thread_.joinable()) {
        db_inserter_thread_.join();
    }

    LOG_INFO_LOC("数据库定时插入线程已停止");
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
                // 其他设备使用基类的 data_dict_rwlock_
                std::shared_lock<std::shared_mutex> lock(device->data_dict_rwlock_);
                
                for (const auto& pair : device->data_dict_) {
                    const std::string& key = pair.first;
                    const RegisterData& reg_data = pair.second;
                    
                    json reg_json = {
                        {"value", reg_data.value},
                        {"unit", reg_data.unit},
                        {"datatype", reg_data.datatype},
                        {"mag", reg_data.mag},
                        {"offset", reg_data.offset}
                    };
                    data_dict_json[key] = reg_json;
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


// ── 从 mqtt_config.json 加载 SN / project_code / control_channel ──
void DeviceManager::loadMqttConfig() {
    try {
        std::ifstream f(Config::MQTT_CONFIG_FILEPATH);
        if (!f.is_open()) {
            LOG_WARNING_LOC("mqtt_config.json 未找到，使用默认配置");
            mqtt_sn_ = "xyc2026002";
            mqtt_project_code_ = "15kW南网户储项目1";
            mqtt_control_channel_ = "cloud/action/xyc2026002/control";
            return;
        }
        json cfg;
        f >> cfg;
        mqtt_sn_ = cfg.value("sn", "xyc2026002");
        mqtt_project_code_ = cfg.value("project_code", "15kW南网户储项目1");
        mqtt_control_channel_ = "cloud/action/" + mqtt_sn_ + "/control";
        LOG_INFO_LOC(("MQTT配置加载: sn=" + mqtt_sn_ +
                      ", channel=" + mqtt_control_channel_).c_str());
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("加载 mqtt_config.json 失败: " + std::string(e.what())).c_str());
    }
}

void DeviceManager::cloudControlSubscribeThread() {
    // 加载 MQTT 配置（sn / channel 等）
    loadMqttConfig();

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

        // 订阅控制频道（从 mqtt_config.json 读取 sn，与 Python 端一致）
        redis_subscriber_->subscribe(mqtt_control_channel_);
        LOG_INFO_LOC(("开始订阅Redis频道: " + mqtt_control_channel_).c_str());

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
        
        // 验证 SN 和 PROJECT_CODE（与 Python 端保持一致，配置从 mqtt_config.json 读取）
        if (json_msg.contains("sn") && json_msg["sn"] != mqtt_sn_) {
            LOG_WARNING_LOC(("SN不匹配: 期望=" + mqtt_sn_ + ", 实际=" + json_msg["sn"].get<std::string>()).c_str());
            return;
        }

        if (json_msg.contains("code") && json_msg["code"] != mqtt_project_code_) {
            LOG_WARNING_LOC(("PROJECT_CODE不匹配: 期望=" + mqtt_project_code_ + ", 实际=" + json_msg["code"].get<std::string>()).c_str());
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
            // 默认处理逻辑：仿照 Python mqtt_controller，直接写入 cmd_from_qt
            // 策略线程的各设备 cmd 模块（ejpcscmd/ejdcdc/emscmd 等）会轮询消费
            if (json_msg.contains("device") && json_msg.contains("command")) {
                std::string device_name = json_msg["device"];
                std::string command = json_msg["command"];
                json value = json_msg.value("value", json(nullptr));

                auto qt = QtController::getInstance();
                std::lock_guard<std::mutex> lock(qt->cmd_mutex_);
                auto& cmd_from_qt = qt->cmd_from_qt;
                cmd_from_qt[device_name][command] = value;

                // 定时模式 / 需求响应模式附带数据写入兄弟 key
                if (command == "sys_setTimer" && json_msg.contains("timingModeSet")) {
                    cmd_from_qt[device_name]["timingModeSet"] = json_msg["timingModeSet"];
                }
                if (command == "sys_setDemandResponse" && json_msg.contains("demandResponseModeSet")) {
                    cmd_from_qt[device_name]["demandResponseModeSet"] = json_msg["demandResponseModeSet"];
                }
                // 透传消息中的其他字段作为兄弟 key（支持未来扩展）
                for (auto& [k, v] : json_msg.items()) {
                    if (k != "device" && k != "command" && k != "value" &&
                        k != "sn" && k != "code" &&
                        k != "timingModeSet" && k != "demandResponseModeSet") {
                        cmd_from_qt[device_name][k] = v;
                    }
                }

                LOG_INFO_LOC(("云端控制 -> cmd_from_qt: [" + device_name + "][" +
                              command + "] = " + value.dump()).c_str());
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
void DeviceManager::initModbusAddressMapping() {
    this->fc04_offsets_.clear();
    this->fc03_map_.clear();

    // --- FC04: 遍历非EMS设备，分配地址（支持自定义起始地址，未设置则按顺序分配） ---
    uint16_t total_input = 0;
    if (this->fc04_enabled_) {
        uint16_t cursor = 0;
        for (const auto& dev : devices_) {
            if (!dev) continue;

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
        total_input = 1;
        for (const auto& kv : this->fc04_offsets_) {
            uint16_t end = kv.second.first + kv.second.second;
            if (end > total_input) total_input = end;
        }
        fc04_total_input_ = total_input;
    } else {
        fc04_total_input_ = 0;
        LOG_INFO_LOC("FC04 已禁用 (fc04_enabled_=false)");
    }

    // --- FC03: EMS data_dict_ 中所有带 tcp_addr 的条目映射到 FC03 ---
    if (ems_) {
        std::shared_lock<std::shared_mutex> lk(ems_->data_dict_rwlock_);
        for (const auto& pair : ems_->data_dict_) {
            const std::string& k = pair.first;
            const RegisterData& rd = pair.second;
            if (rd.tcp_addr == 0xFFFF) continue;    // 跳过0xFFFF的条目

            Fc03Mapping m;
            m.device_name = "ems";
            m.key      = k;
            m.mag      = rd.mag;
            m.offset   = rd.offset;
            m.datatype = rd.datatype;
            m.reg_count = reg_count_of(m.datatype);
            m.rtu_addr = 0;  // EMS是虚拟设备，无RTU地址
            m.skip_count = 0; m.last_val[0] = m.last_val[1] = 0;
            m.writable = rd.writable;

            uint16_t out[2]; int dummy;     // dummy作用为根据datatype确定是否需要两个寄存器
            value_to_regs(rd.value, m.mag, m.offset, m.datatype, out, dummy);   // 将data_dict的当前值转换为TCP寄存器表示
            m.last_val[0] = out[0];     // 记录最新更新的值
            if (m.reg_count > 1) m.last_val[1] = out[1];     // 记录最新更新的值    

            this->fc03_map_[rd.tcp_addr] = m;       // 保存到映射表中
        }
    }


    // ── 根据 EMS 配置文件动态计算定时/需求响应条目数 ──
    timer_charge_entries_    = 0;
    timer_discharge_entries_ = 0;
    demand_entries_          = 0;
    if (ems_) {
        std::shared_lock<std::shared_mutex> lk(ems_->json_rwlock_);
        if (ems_->timingModeSet.contains("chargeTimeList") && ems_->timingModeSet["chargeTimeList"].is_array())
            timer_charge_entries_ = static_cast<int>(ems_->timingModeSet["chargeTimeList"].size());
        if (ems_->timingModeSet.contains("dischargeTimeList") && ems_->timingModeSet["dischargeTimeList"].is_array())
            timer_discharge_entries_ = static_cast<int>(ems_->timingModeSet["dischargeTimeList"].size());
        if (ems_->demandResponseModeSet.is_array())
            demand_entries_ = static_cast<int>(ems_->demandResponseModeSet.size());
    }
    // 确保至少有一定空间
    if (timer_charge_entries_ < 1)    timer_charge_entries_ = 1;
    if (timer_discharge_entries_ < 1) timer_discharge_entries_ = 1;
    if (demand_entries_ < 1)          demand_entries_ = 1;
    int total_timer_entries  = timer_charge_entries_ + timer_discharge_entries_;
    int total_timer_regs     = total_timer_entries * TIMER_ENTRY_REGS;
    int total_demand_regs    = demand_entries_ * DEMAND_ENTRY_REGS;

    // 计算 total_holding = max(fc03_end, timer_block_end, demand_block_end)
    // --- FC03: 为非EMS设备分配地址 ---
    // 用于自动分配的游标：从定时/需求块之后开始
    // 先用 fc04 cursor 或一个安全起点
    uint16_t fc03_cursor = 5000;  // 自动分配起始，避免与EMS/定时/需求块重叠

    for (const auto& dev : devices_) {
        if (!dev) continue;
        if (dev->get_name() == "ems") continue;  // EMS 已处理

        const auto& keys = dev->dev_data_keys_;
        if (keys.empty()) continue;

        // 确定起始地址
        auto custom_it = this->fc03_start_addrs_.find(dev->get_name());
        uint16_t start = (custom_it != this->fc03_start_addrs_.end())
                             ? custom_it->second
                             : fc03_cursor;

        // 计算所需寄存器数: addr0=online_status + 每个 data_dict 条目
        uint16_t cnt = 1;  // online_status
        {
            std::shared_lock<std::shared_mutex> lk(dev->data_dict_rwlock_);
            const auto& dict = dev->data_dict_;
            for (const auto& k : keys) {
                auto it = dict.find(k);
                if (it != dict.end())
                    cnt += reg_count_of(it->second.datatype);
            }
        }
        if (cnt <= 1) continue;  // 只有online_status，无意义

        uint16_t end = start + cnt - 1;

        // 冲突检测：新范围 [start, end] 与已有 fc03_map_ 是否重叠
        bool conflict = false;
        for (const auto& kv : this->fc03_map_) {
            uint16_t a = kv.first;
            uint16_t b = kv.first + kv.second.reg_count - 1;
            if (!(end < a || start > b)) {
                LOG_ERROR_LOC(("FC03 [" + dev->get_name() + "] addr " +
                               std::to_string(start) + "~" + std::to_string(end) +
                               " 与已有映射 addr " + std::to_string(a) +
                               " 重叠，跳过").c_str());
                conflict = true;
                break;
            }
        }
        // 检查是否与定时模式块重叠
        if (!conflict) {
            uint16_t t_start = timer_block_start_addr_;
            uint16_t t_end   = t_start + total_timer_regs - 1;
            if (!(end < t_start || start > t_end)) {
                LOG_ERROR_LOC(("FC03 [" + dev->get_name() + "] addr " +
                               std::to_string(start) + "~" + std::to_string(end) +
                               " 与定时模式块重叠，跳过").c_str());
                conflict = true;
            }
        }
        // 检查是否与需求响应块重叠
        if (!conflict) {
            uint16_t d_start = demand_block_start_addr_;
            uint16_t d_end   = d_start + total_demand_regs - 1;
            if (!(end < d_start || start > d_end)) {
                LOG_ERROR_LOC(("FC03 [" + dev->get_name() + "] addr " +
                               std::to_string(start) + "~" + std::to_string(end) +
                               " 与需求响应块重叠，跳过").c_str());
                conflict = true;
            }
        }
        if (conflict) continue;

        // 创建映射
        uint16_t addr = start;

        // addr+0: online_status
        {
            Fc03Mapping m;
            m.device_name = dev->get_name();
            m.key         = "online_status";
            m.mag         = 1.0;
            m.offset      = 0;
            m.datatype    = "UINT16";
            m.reg_count   = 1;
            m.last_val[0] = dev->online_status.load() ? 1 : 0;
            m.last_val[1] = 0;
            m.skip_count  = 0;
            this->fc03_map_[addr] = m;
            addr++;
        }

        // 依次映射 data_dict 条目
        {
            std::shared_lock<std::shared_mutex> lk(dev->data_dict_rwlock_);
            const auto& dict = dev->data_dict_;
            for (const auto& k : keys) {
                auto it = dict.find(k);
                if (it == dict.end()) continue;

                const RegisterData& rd = it->second;
                Fc03Mapping m;
                m.device_name = dev->get_name();
                m.key         = k;
                m.mag         = rd.mag;
                m.offset      = rd.offset;
                m.datatype    = rd.datatype;
                m.skip_count = 0; m.last_val[0] = m.last_val[1] = 0;
                m.reg_count   = reg_count_of(rd.datatype);
                m.rtu_addr    = rd.address;  // RTU原始modbus地址
                // 判断该key的RTU地址原始功能码，决定写回方式:
                //   0=无RTU来源  1=FC01(线圈→FC05)  3=FC03(保持寄存器→FC06/16)
                //   2/4=只读来源(FC02离散输入/FC04输入寄存器)→拒绝写入
                //
                // 优先检测 CAN 设备的可写控制键（这些键在 fc03_nameToAddr_map 中但应走 CAN 而非 RTU 写回）
                bool is_can_device = (Config::CAN_INTERFACES.find(dev->get_com()) != Config::CAN_INTERFACES.end());
                if (is_can_device) {
                    static const std::unordered_set<std::string> can_ctrl_keys = {
                        "充电机开关机", "充电机设置电压(V)", "充电机设置电流(A)"
                    };
                    if (can_ctrl_keys.find(k) != can_ctrl_keys.end()) {
                        m.can_device_ctrl = true;
                        LOG_INFO_LOC(("FC03 CAN控制键: [" + dev->get_name() + "][" + k +
                                      "] 标记为可写 (CAN直控)").c_str());
                    }
                    // CAN 设备不设置 original_fc（无 RTU 写回）
                } else if (dev->fc03_nameToAddr_map.find(k) != dev->fc03_nameToAddr_map.end()) {
                    m.original_fc = 3;
                } else if (dev->fc01_nameToAddr_map.find(k) != dev->fc01_nameToAddr_map.end()) {
                    m.original_fc = 1;
                }
                // FC02/FC04 保持 original_fc=0，syncAllFc03中跳过写回

                int dummy;
                uint16_t out[2];    // 实际数据转化为out[2]数组的寄存器值
                value_to_regs(rd.value, m.mag, m.offset, m.datatype, out, dummy);
                m.last_val[0] = out[0];
                if (m.reg_count > 1) m.last_val[1] = out[1];

                this->fc03_map_[addr] = m;
                addr += m.reg_count;
            }
        }

        // 未自定义的设备才推进自动游标
        if (custom_it == this->fc03_start_addrs_.end()) {
            fc03_cursor = addr;
        }

        LOG_INFO_LOC(("FC03 [" + dev->get_name() + "] addr " +
                      std::to_string(start) + "~" + std::to_string(addr - 1) +
                      " (" + std::to_string(addr - start) + " regs, rtu_writeback=" +
                      (dev->get_com() <= 7 ? "enabled" : "disabled") + ")").c_str());
    }
    uint16_t total_holding = 1;
    if (!this->fc03_map_.empty()) {
        auto last = this->fc03_map_.rbegin();       // 获取最后一个映射项
        total_holding = last->second.reg_count > 1  // 统计最大地址的最后一个值是1个寄存器还是2个寄存器
                            ? last->first + 2
                            : last->first + 1;
    }

    // 比较最大地址的值和定时/需求响应条目数的地址，取较大值为最终03功能码区的数量
    uint16_t timer_block_end  = timer_block_start_addr_ + total_timer_regs;
    uint16_t demand_block_end = demand_block_start_addr_ + total_demand_regs;
    if (timer_block_end > total_holding)  total_holding = timer_block_end;
    if (demand_block_end > total_holding) total_holding = demand_block_end;
    fc03_total_holding_ = total_holding;

    // 初始化 last 缓存
    last_timer_block_.assign(total_timer_regs, 0);
    last_demand_block_.assign(total_demand_regs, 0);

    LOG_INFO_LOC(("Modbus地址映射完成: total_holding=" + std::to_string(total_holding) +
                  ", total_input=" + std::to_string(total_input) +
                  ", timer(charge=" + std::to_string(timer_charge_entries_) +
                  "+discharge=" + std::to_string(timer_discharge_entries_) +
                  ")@" + std::to_string(timer_block_start_addr_) +
                  ", demand(" + std::to_string(demand_entries_) +
                  ")@" + std::to_string(demand_block_start_addr_) +
                  ", fc03_mappings=" + std::to_string(this->fc03_map_.size())).c_str());
}

void DeviceManager::initModbusTcpServer(const std::string& ip, int port) {
    initModbusAddressMapping();

    // 创建 TCP ModbusServer 并分配数据区
    this->modbus_tcp_server_ = std::make_unique<ModbusServer>(ip, std::to_string(port), 10);
    this->modbus_tcp_server_->init_data_area(0, 0, fc03_total_holding_, fc04_total_input_);

    LOG_INFO_LOC(("ModbusTCP server初始化: " + ip + ":" + std::to_string(port)).c_str());
}

void DeviceManager::initModbusRtuServer(const std::string& port, int baudrate, int slave_id) {
    initModbusAddressMapping();

    // 创建 RTU ModbusServer 并分配数据区
    this->modbus_rtu_server_ = std::make_unique<ModbusServer>(port, baudrate, slave_id);
    this->modbus_rtu_server_->init_data_area(0, 0, fc03_total_holding_, fc04_total_input_);

    LOG_INFO_LOC(("ModbusRTU server初始化: port=" + port +
                  ", baudrate=" + std::to_string(baudrate) +
                  ", slave_id=" + std::to_string(slave_id)).c_str());
}

// ── 启动 ──
void DeviceManager::startModbusTcpServer() {
    std::string ip = "0.0.0.0";
    int port = 1026;
    // 设置每个设备的 FC04 ModbusTcp服务器起始地址
    // setDeviceFc04StartAddr("ems", 0);
    // setDeviceFc04StartAddr("pcs1", 1000);
    // setDeviceFc04StartAddr("dcdc1", 1300);
    // setDeviceFc04StartAddr("dcdc2", 1400);
    // setDeviceFc04StartAddr("chargers", 1500);
    // setDeviceFc04StartAddr("dtsd3366", 1600);
    // setDeviceFc04StartAddr("air_condition", 1800);
    // // board_8di8do removed: no IO module hardware on this EMS
    // setDeviceFc04StartAddr("gtbms485", 2000);
    // setDeviceFc04StartAddr("dg_hgm6100n", 3000);


    // 设置每个设备的 FC03 ModbusTcp服务器起始地址（非EMS设备）
    setDeviceFc03StartAddr("ems", 0);
    setDeviceFc03StartAddr("pcs1", 1000);
    setDeviceFc03StartAddr("dcdc1", 1300);
    setDeviceFc03StartAddr("dcdc2", 1400);
    setDeviceFc03StartAddr("chargers", 1500);
    setDeviceFc03StartAddr("dtsd3366", 1600);
    setDeviceFc03StartAddr("air_condition", 1800);
    // board_8di8do removed: no IO module hardware on this EMS
    setDeviceFc03StartAddr("gtbms485", 2000);
    setDeviceFc03StartAddr("dg_hgm6100n", 3000);

    initModbusTcpServer(ip, port);

    // 先写一次初始值
    // syncAllFc04();
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

void DeviceManager::stopModbusRtuServer() {
    // 仅当TCP server也未运行时才停同步线程
    if (!this->modbus_tcp_server_) {
        modbus_sync_running_ = false;
        if (this->modbus_sync_thread_.joinable()) this->modbus_sync_thread_.join();
    }

    if (this->modbus_rtu_server_) this->modbus_rtu_server_->stop();
    LOG_INFO_LOC("Modbus RTU server stopped");
}

// ── 启动 RTU 服务器 ──
void DeviceManager::startModbusRtuServer() {
    std::string port = "/dev/ttyS0";    // 默认485串口
    int baudrate = 9600;
    int slave_id = 1;

    // 如果映射尚未初始化（如TCP已先启动则复用），则初始化
    if (fc03_map_.empty() && fc04_offsets_.empty()) {
        setDeviceFc04StartAddr("ems", 0);
        setDeviceFc04StartAddr("pcs1", 1000);
        setDeviceFc04StartAddr("dcdc1", 1300);
        setDeviceFc04StartAddr("dcdc2", 1400);
        setDeviceFc04StartAddr("chargers", 1500);
        setDeviceFc04StartAddr("dtsd3366", 1600);
        setDeviceFc04StartAddr("air_condition", 1800);
        setDeviceFc04StartAddr("gtbms485", 2000);
        setDeviceFc04StartAddr("dg_hgm6100n", 3000);

        setDeviceFc03StartAddr("pcs1", 1000);
        setDeviceFc03StartAddr("dcdc1", 1300);
        setDeviceFc03StartAddr("dcdc2", 1400);
        setDeviceFc03StartAddr("chargers", 1500);
        setDeviceFc03StartAddr("dtsd3366", 1600);
        setDeviceFc03StartAddr("air_condition", 1800);
        setDeviceFc03StartAddr("gtbms485", 2000);
        setDeviceFc03StartAddr("dg_hgm6100n", 3000);

        initModbusRtuServer(port, baudrate, slave_id);
    } else {
        // 复用已有映射，仅创建RTU server
        this->modbus_rtu_server_ = std::make_unique<ModbusServer>(port, baudrate, slave_id);
        this->modbus_rtu_server_->init_data_area(0, 0, fc03_total_holding_, fc04_total_input_);
        LOG_INFO_LOC(("ModbusRTU server复用已有映射: port=" + port +
                      ", baudrate=" + std::to_string(baudrate) +
                      ", slave_id=" + std::to_string(slave_id)).c_str());
    }

    // 先写一次初始值
    // syncAllFc04To(modbus_rtu_server_.get());
    syncAllFc03To(modbus_rtu_server_.get());
    syncTimerBlockTo(modbus_rtu_server_.get());
    syncDemandBlockTo(modbus_rtu_server_.get());

    if (!this->modbus_rtu_server_->start()) {
        LOG_ERROR_LOC("Modbus RTU server启动失败！");
        return;
    }

    // 如果同步线程尚未运行（如TCP已先启动则复用），则启动
    if (!this->modbus_sync_running_) {
        this->modbus_sync_running_ = true;
        this->modbus_sync_thread_ = std::thread(&DeviceManager::modbusSyncLoop, this);
    }
    LOG_INFO_LOC("Modbus RTU server启动成功:" + port +
                 " baudrate=" + std::to_string(baudrate) +
                 " slave_id=" + std::to_string(slave_id));
}

// ── 后台同步线程 (≈1秒) ──
void DeviceManager::modbusSyncLoop() {
    while (this->modbus_sync_running_) {
        for (int i = 0; i < 10 && this->modbus_sync_running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!this->modbus_sync_running_) break;

        try {
            if (modbus_tcp_server_) {
                if (fc04_enabled_) syncAllFc04To(modbus_tcp_server_.get());
                syncAllFc03To(modbus_tcp_server_.get());
                syncTimerBlockTo(modbus_tcp_server_.get());
                syncDemandBlockTo(modbus_tcp_server_.get());
            }
            if (modbus_rtu_server_) {
                if (fc04_enabled_) syncAllFc04To(modbus_rtu_server_.get());
                syncAllFc03To(modbus_rtu_server_.get());
                syncTimerBlockTo(modbus_rtu_server_.get());
                syncDemandBlockTo(modbus_rtu_server_.get());
            }
        } catch (const std::exception& e) {
            LOG_ERROR_LOC(("Modbus服务器同步错误: " + std::string(e.what())).c_str());
        }
    }
}

// ── FC04: 将设备 data_dict 同步到输入寄存器 ──
void DeviceManager::syncAllFc04To(ModbusServer* server) {
    if (!server) return;

    // 优化：先复制所有设备数据，减少持锁时间
    struct DeviceData {
        std::string name;
        uint16_t start_addr;
        bool online;
        std::vector<std::pair<std::string, RegisterData>> registers;
    };

    std::vector<DeviceData> all_devices_data;

    for (const auto& dev : devices_) {
        if (!dev) continue;

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
        server->set_input_register(dev_data.start_addr,
                                   dev_data.online ? 1 : 0);
        uint16_t addr = dev_data.start_addr + 1;

        for (const auto& reg_pair : dev_data.registers) {
            const RegisterData& rd = reg_pair.second;
            uint16_t out[2]; int cnt;
            value_to_regs(rd.value, rd.mag, rd.offset, rd.datatype, out, cnt);

            if (cnt >= 2)
                server->set_input_registers(addr, 2, out);
            else
                server->set_input_register(addr, out[0]);
            addr += cnt;
        }
    }
}

// ── FC03: 双边同步（待决追踪 + 速率限制）──
// 写入方向: HR变化检测 → 写回RTU + 设置待决追踪（期望值+3秒超时）
// ── FC03: 双边同步（skip_count 防回弹）──
// 写入方向: 检测HR变化 → 写RTU → skip_count=3 跳过后续推送等data_dict追上
// 推送方向: skip_count>0 则递减并跳过；否则 data_dict → HR
void DeviceManager::syncAllFc03To(ModbusServer* server) {
    if (!server) return;
    if (this->fc03_map_.empty()) return;

    bool server_running = server->is_running();
    auto hr = server->get_holding_registers(0, fc03_total_holding_);
    if (hr.size() < fc03_total_holding_) return;

    struct CW { std::string key; double real; };
    std::vector<CW> ems_writes;

    for (auto& pair : fc03_map_) {
        const uint16_t addr = pair.first;
        Fc03Mapping& m = pair.second;
        if (addr >= hr.size()) continue;        // 验证addr是否大于最大地址

        uint16_t cur[2] = {hr[addr], 0};
        if (m.reg_count > 1 && addr + 1 < hr.size()) cur[1] = hr[addr + 1];

        // changed比较当前HR块cur是否和上次循环的缓存last_val一样，不一样则表示客户端写入，一样则判断EMS有没有更新
        bool changed = server_running && ((cur[0] != m.last_val[0]) ||
                        (m.reg_count > 1 && cur[1] != m.last_val[1]));

        if (changed) {
            // ── 客户端写入 ──
            double real = regs_to_value(cur, m.reg_count, m.mag, m.offset);
            // ems特殊判断，只写writable为true的可写寄存器
            if (m.device_name == "ems") {
                if (!m.writable) {
                    LOG_WARNING_LOC(("FC03 拒绝写入只读EMS寄存器: [" + m.key +
                                     "] tcp=" + std::to_string(addr)).c_str());
                } else {
                    ems_writes.push_back({m.key, real});
                }
            } else if (m.can_device_ctrl) {
                // ── CAN 设备控制：通过虚方法派发，无需知道具体 charger 类型 ──
                auto dev = getDeviceByName(m.device_name);
                if (dev) {
                    dev->updateRegisterValue(m.key, real);
                    dev->setCanControlParam(m.key, real);
                    auto can_ops = getCanOperators();
                    auto op_it = can_ops.find(static_cast<int>(dev->get_com()));
                    if (op_it != can_ops.end() && op_it->second) {
                        dev->sendCanControlFrames(op_it->second);
                    }
                }
                m.skip_count = FC03_SKIP_CYCLES;
                LOG_INFO_LOC(("FC03 CAN控制: [" + m.device_name + "][" + m.key +
                              "] tcp=" + std::to_string(addr) +
                              " real=" + std::to_string(real) +
                              " skip=" + std::to_string(FC03_SKIP_CYCLES)).c_str());
            } else if (m.original_fc >= 1) {
                // modbus设备
                auto dev = getDeviceByName(m.device_name);
                if (m.original_fc == 1) {
                    // ── FC01 来源：线圈 → 用 FC05 写回（0→OFF, 非0→ON）──
                    auto mb = mapComToModbusClient.find(static_cast<int>(dev ? dev->get_com() : 0));
                    if (mb != mapComToModbusClient.end() && mb->second && mb->second->is_connected() && dev) {
                        // dev->updateRegisterValue(m.key, real);
                        mb->second->set_slave(dev->get_id());
                        bool coil_val = (real != 0.0);
                        bool ok = mb->second->write_coil(m.rtu_addr, coil_val);
                        if (ok) {
                            m.skip_count = FC03_SKIP_CYCLES;
                            LOG_INFO_LOC(("FC03 写回(FC05线圈): [" + m.device_name + "][" + m.key +
                                          "] tcp=" + std::to_string(addr) + " rtu=" + std::to_string(m.rtu_addr) +
                                          " coil=" + std::to_string(coil_val) + " real=" + std::to_string(real) +
                                          " skip=" + std::to_string(FC03_SKIP_CYCLES)).c_str());
                        }
                    }
                } else if (m.original_fc == 3) {
                    // ── FC03 来源：保持寄存器 → 用 FC06/FC16 写回 ──
                    auto mb = mapComToModbusClient.find(static_cast<int>(dev ? dev->get_com() : 0));
                    if (mb != mapComToModbusClient.end() && mb->second && mb->second->is_connected() && dev) {
                        // dev->updateRegisterValue(m.key, real);
                        mb->second->set_slave(dev->get_id());
                        bool ok = (m.reg_count >= 2) ? mb->second->write_registers(m.rtu_addr, 2, cur)
                                                       : mb->second->write_register(m.rtu_addr, cur[0]);
                        if (ok) {
                            m.skip_count = FC03_SKIP_CYCLES;
                            LOG_INFO_LOC(("FC03 写回(FC06/16): [" + m.device_name + "][" + m.key +
                                          "] tcp=" + std::to_string(addr) + " rtu=" + std::to_string(m.rtu_addr) +
                                          " raw=" + std::to_string(cur[0]) + " real=" + std::to_string(real) +
                                          " skip=" + std::to_string(FC03_SKIP_CYCLES)).c_str());
                        }
                    }
                } else {
                    // ── FC02/FC04/未知来源：只读，拒绝写入 data_dict ──
                    // 实际设备的data_dict由读取线程更新，TCP客户端写入不应覆盖
                    LOG_WARNING_LOC(("FC03 拒绝写入(只读来源 fc=" + std::to_string(m.original_fc) +
                                     "): [" + m.device_name + "][" + m.key +
                                     "] tcp=" + std::to_string(addr) +
                                     " rtu=" + std::to_string(m.rtu_addr) +
                                     " real=" + std::to_string(real)).c_str());
                }
            }
            m.last_val[0] = cur[0];
            if (m.reg_count > 1) m.last_val[1] = cur[1];
        } else {
            // ── 推送 data_dict → HR ──
            if (m.skip_count > 0) { m.skip_count--; continue; }  // 等待 RTU 确认

            double value = 0.0;
            // 获取设备的data_dict的值赋值给value
            if (m.device_name == "ems")
                value = ems_ ? ems_->getValue<double>(m.key, 0.0) : 0.0;
            else if (m.key == "online_status") {
                auto dev = getDeviceByName(m.device_name);
                value = dev ? (dev->online_status.load() ? 1.0 : 0.0) : 0.0;
            } else {
                auto dev = getDeviceByName(m.device_name);
                value = dev ? dev->getValue<double>(m.key, 0.0) : 0.0;
            }

            uint16_t out[2]; int cnt;
            value_to_regs(value, m.mag, m.offset, m.datatype, out, cnt);    // value转换为HR块
            // out：data_dict的值，cur：当前HR块的值
            if (out[0] == cur[0] && (cnt < 2 || m.reg_count < 2 || out[1] == cur[1])) continue;

            if (cnt >= 2)
                server->set_holding_registers(addr, 2, out);    // 更新HR块
            else
                server->set_holding_register(addr, out[0]);     // 更新HR块

            uint16_t c[2];
            server->get_holding_register(addr, &c[0]);
            if (cnt >= 2 && m.reg_count > 1) server->get_holding_register(addr + 1, &c[1]);
            m.last_val[0] = c[0];
            if (m.reg_count > 1) m.last_val[1] = c[1];
        }
    }

    // EMS 持久化
    if (!ems_writes.empty() && server_running && ems_) {
        std::unique_lock<std::shared_mutex> lk(ems_->data_dict_rwlock_);
        json data;
        for (auto& w : ems_writes) {
            auto dit = ems_->data_dict_.find(w.key);
            if (dit != ems_->data_dict_.end()) dit->second.value = w.real;
            data[w.key] = w.real;
        }
        // 写入客户端写入变化的data_dict到配置文件
        if (!data.empty()) ems_->write_jsonfile_nolock(data);
    }
}
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
void DeviceManager::syncTimerBlockTo(ModbusServer* server) {
    if (!server || !ems_) return;

    int charge_cnt    = timer_charge_entries_;
    int discharge_cnt = timer_discharge_entries_;
    size_t total = (timer_charge_entries_ + timer_discharge_entries_) * TIMER_ENTRY_REGS;

    // 1. 读当前 HR 块
    std::vector<uint16_t> cur(total);
    for (size_t i = 0; i < total; ++i) {
        uint16_t v;
        if (!server->get_holding_register(
                timer_block_start_addr_ + i, &v)) return;
        cur[i] = v;
    }

    // 辅助 lambda：编码一条定时记录到 vector<uint16_t> regs，解码tingmingModeSet到regs
    auto encode_entry = [](std::vector<uint16_t>& regs, int& idx, const json& entry) {
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
    };

    // 辅助 lambda：解码 regs 到 JSON entry，传入的cur_ptr指向当前记录vector的起始地址，regs->tingmingModeSet
    auto decode_entry = [](const uint16_t* cur_ptr) -> json {
        int sh = cur_ptr[0], sm = cur_ptr[1], eh = cur_ptr[2], em = cur_ptr[3]; // startHour, startMinute, endHour, endMinute
        int wd = cur_ptr[4];    // weekday
        int16_t power = static_cast<int16_t>(cur_ptr[5]);
        char sb[6], eb[6];
        snprintf(sb, sizeof(sb), "%02d:%02d", sh, sm);
        snprintf(eb, sizeof(eb), "%02d:%02d", eh, em);
        return {{"startTime", sb}, {"endTime", eb},
                {"weekday", int_to_weekday_json(wd)}, {"power", power}};
    };

    // 比较上次循环的缓存，是否和当前循环的HR块一样，不一样则表示客户端写入，一样则判断EMS有没有更新，cur:当前HR块
    bool client_wrote = (cur != last_timer_block_);     
    if (!client_wrote) {
        // 2a. 无外部写入 → 读 EMS，推送到 HR
        json timing;
        // 加锁复制，减少持锁时间
        {
            std::shared_lock<std::shared_mutex> lk(ems_->json_rwlock_);
            timing = ems_->timingModeSet;
        }

        // regs临时存储当前EMS的timingModeSet和demandResponseModeSet的寄存器值，最后写入HR块
        std::vector<uint16_t> regs(total, 0);
        int idx = 0;

        // chargeTimeList
        if (timing.contains("chargeTimeList") && timing["chargeTimeList"].is_array()) {
            for (const auto& entry : timing["chargeTimeList"]) {
                if (idx >= charge_cnt * TIMER_ENTRY_REGS) break;
                encode_entry(regs, idx, entry);
            }
        }
        idx = charge_cnt * TIMER_ENTRY_REGS;  // discharge 从 charge 之后开始

        // dischargeTimeList
        if (timing.contains("dischargeTimeList") && timing["dischargeTimeList"].is_array()) {
            for (const auto& entry : timing["dischargeTimeList"]) {
                if (idx >= static_cast<int>(total)) break;
                encode_entry(regs, idx, entry);
            }
        }

        // 写入前 recheck HR
        for (size_t i = 0; i < total; ++i) {
            uint16_t v;
            if (!server->get_holding_register(
                    timer_block_start_addr_ + i, &v)) return;
            cur[i] = v;
        }

        if (cur != last_timer_block_) {
            client_wrote = true;        // 外部写入不同的HR块
        } else if (regs != last_timer_block_) {     // regs: 当前EMS的timingModeSet和demandResponseModeSet的寄存器值
            // 2c. 无外部写入，但EMS更新了定时/需求响应模式块，需要更新HR块
            for (size_t i = 0; i < total; ++i)
                server->set_holding_register(timer_block_start_addr_ + i, regs[i]);
            for (size_t i = 0; i < total; ++i) {
                uint16_t v;
                server->get_holding_register(timer_block_start_addr_ + i, &v);
                cur[i] = v;
            }
            last_timer_block_ = cur;
            return;
        } else {
            return;
        }
    }

    // 2b. 客户端写入 → 解析并更新 EMS
    {
        json charge_list = json::array();
        json discharge_list = json::array();

        // 前 charge_cnt 条是 chargeTimeList
        for (int i = 0; i < charge_cnt; i++) {
            size_t off = i * TIMER_ENTRY_REGS;
            if (off + TIMER_ENTRY_REGS > cur.size()) break;
            bool all_zero = true;
            for (int j = 0; j < TIMER_ENTRY_REGS; ++j)
                if (cur[off + j] != 0) { all_zero = false; break; }
            if (all_zero) continue;
            charge_list.push_back(decode_entry(&cur[off]));
        }

        // 后 discharge_cnt 条是 dischargeTimeList
        for (int i = 0; i < discharge_cnt; i++) {
            size_t off = charge_cnt * TIMER_ENTRY_REGS + i * TIMER_ENTRY_REGS;
            if (off + TIMER_ENTRY_REGS > cur.size()) break;
            bool all_zero = true;
            for (int j = 0; j < TIMER_ENTRY_REGS; ++j)
                if (cur[off + j] != 0) { all_zero = false; break; }
            if (all_zero) continue;
            discharge_list.push_back(decode_entry(&cur[off]));
        }
        {
            std::unique_lock<std::shared_mutex> lk(ems_->json_rwlock_);
            ems_->timingModeSet["chargeTimeList"]    = charge_list;
            ems_->timingModeSet["dischargeTimeList"] = discharge_list;
        }
        ems_->write_timerJsonFile(
            json{{"timingModeSet", ems_->timingModeSet}},
            Config::EMS_CONFIG_FILEPATH_JSON);
    }

    last_timer_block_ = cur;
    LOG_INFO_LOC("定时模式块同步: charge=" + std::to_string(charge_cnt) +
                 " discharge=" + std::to_string(discharge_cnt));
}

// ── 需求响应模式块：双向同步（检测客户端写入→EMS, EMS→保持寄存器），单趟消除竞态 ──
void DeviceManager::syncDemandBlockTo(ModbusServer* server) {
    if (!server || !ems_) return;

    size_t total = demand_entries_ * DEMAND_ENTRY_REGS;

    // 1. 读当前 HR 块
    std::vector<uint16_t> cur(total);
    for (size_t i = 0; i < total; ++i) {
        uint16_t v;
        if (!server->get_holding_register(
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
            if (!server->get_holding_register(
                    demand_block_start_addr_ + i, &v)) return;
            cur[i] = v;
        }

        if (cur != last_demand_block_) {
            client_wrote = true;
        } else if (regs != last_demand_block_) {
            for (size_t i = 0; i < total; ++i)
                server->set_holding_register(
                    demand_block_start_addr_ + i, regs[i]);

            for (size_t i = 0; i < total; ++i) {
                uint16_t v;
                server->get_holding_register(
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
        {
            std::unique_lock<std::shared_mutex> lk(ems_->json_rwlock_);
            ems_->demandResponseModeSet = demand_list;
        }
        ems_->write_timerJsonFile(
            json{{"demandResponseModeSet", ems_->demandResponseModeSet}},
            Config::EMS_CONFIG_FILEPATH_JSON);
    }

    last_demand_block_ = cur;
    LOG_INFO_LOC("检测到远程需求响应模式块写入，已同步");
}