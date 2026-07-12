#ifndef DEVICE_H
#define DEVICE_H

#include <iostream>
#include <memory>
#include <vector>
#include <shared_mutex>  // C++17 读写锁支持
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <functional>
#include "json.hpp"
#include "canoperator.h"
#include "modbusclient.h"
#include <pugixml.hpp>
#include "log.h"
#include "utils.h"

using json = nlohmann::json;

struct AlarmDebounceTimer {
    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> cancelled_{false};

    ~AlarmDebounceTimer() {
        cancel();
    }

    void cancel() {
        bool expected = false;
        if (cancelled_.compare_exchange_strong(expected, true)) {
            cv_.notify_one();
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool is_cancelled() const {
        return cancelled_.load();
    }
};

struct RegisterData {
    uint16_t address = 0;
    double value = 0.0;
    double mag = 1.0;
    uint16_t offset = 0;
    std::string datatype;
    std::string unit;
    uint8_t register_count = 1;
    bool big_endian = true;
    uint16_t tcp_addr = 0xFFFF;  // Modbus TCP 地址 (0xFFFF=不映射)
    bool writable = false;       // FC03 是否可写
};

class Device {
public:
    struct RegisterSegment {
        uint16_t start_addr;
        uint16_t num_regs;
    };

    struct ParsedRegister {
        std::string key;
        uint16_t buffer_index;
    };

    Device(const std::string& name, int com, int id)
        : name_(name), com_(com), id_(id) {}

    virtual ~Device();

    virtual void parse_rawdata(const std::vector<uint16_t>& data_list) {
        parse_rawdata_generic(data_list);
    }

    virtual void read_data(ModbusClient& mb_client)=0;

    /**
     * @brief 更新设备的告警状态，通常在解析完原始数据后调用
     */
    virtual void update_alarm_status() {}
    /**
     * @brief 供 CAN 设备实现的读取逻辑入口
     * @param can_operator 注入的 CAN 操作对象
     */
    virtual void read_data(CanOperator& can_operator) {}

    /**
     * @brief 供 CAN 设备实现的批量写入逻辑入口
     * @param can_operator 注入的 CAN 操作对象
     */
    virtual void multiWriteCmdToDevice(std::shared_ptr<CanOperator> can_operator) {}

    /**
     * @brief 供 CAN 设备实现：FC03 写入时路由到设备控制参数 setter
     * @param key   data_dict 键名（如 "开关机控制"、"电压设定(V)"）
     * @param value 真实值（已通过 mag/offset 反算）
     */
    virtual void setCanControlParam(const std::string& key, double value) {}

    /**
     * @brief 供 CAN 设备实现：发送 CAN 控制帧
     * @param can_operator CAN 操作对象
     */
    virtual void sendCanControlFrames(std::shared_ptr<CanOperator> can_operator) {}

    // JSON结构初始化 - 提供默认实现
    virtual void init_json_structure(const std::string& name) {
        this->data_to_qt = {
            {"name", name},
            {"online_status", false},
            {"timestamp", ""},
            {"data", json::array()},
        };
    }

    // 配置初始化 - 提供默认实现，子类可重写
    virtual void init_config(const std::string& config_file) {
        LOG_INFO_LOC(("加载"+this->name_+"配置文件: " + config_file).c_str());
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(config_file.c_str());

        if (!result) {
            LOG_ERROR_LOC(("加载"+this->name_+"配置文件失败: " + config_file + ", Error: " + result.description()).c_str());
            return;
        }

        pugi::xml_node root = doc.document_element();
        if (!root) {
            LOG_ERROR_LOC(("加载"+this->name_+"配置文件XML格式错误").c_str());
            return;
        }

        // 解析功能码01（离散输出）
        pugi::xml_node fc01_node = root.child("function_code01");
        if (fc01_node) {
            for (pugi::xml_node coil : fc01_node.children("coil")) {
                std::string name = coil.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(coil.attribute("address").as_string()));
                    reg_data.datatype = coil.attribute("datatype").as_string();
                    reg_data.value = 0.0;

                    // 存储到映射和字典中
                    this->fc01_nameToAddr_map[name] = reg_data.address;
                    this->data_dict_[name] = reg_data;
                    this->dev_data_keys_.push_back(name);
                }
            }
        }

        // 解析功能码02（离散输入）
        pugi::xml_node fc02_node = root.child("function_code02");
        if (fc02_node) {
            for (pugi::xml_node di : fc02_node.children("di")) {
                std::string name = di.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(di.attribute("address").as_string()));
                    reg_data.datatype = di.attribute("datatype").as_string();
                    reg_data.value = 0.0;

                    // 存储到映射和字典中
                    this->fc02_nameToAddr_map[name] = reg_data.address;
                    this->data_dict_[name] = reg_data;
                    this->dev_data_keys_.push_back(name);
                }
            }
        }


        // 解析功能码03（保持寄存器）
        pugi::xml_node fc03_node = root.child("function_code03");
        if (fc03_node) {
            for (pugi::xml_node hr : fc03_node.children("hRegister")) {
                std::string name = hr.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(hr.attribute("address").as_string()));
                    reg_data.mag =  static_cast<double>(std::stoi(hr.attribute("mag").as_string()));
                    reg_data.offset =  static_cast<uint16_t>(std::stoi(hr.attribute("offset").as_string()));
                    reg_data.datatype = hr.attribute("datatype").as_string();
                    reg_data.unit = hr.attribute("unit").as_string();
                    reg_data.value = 0.0;
                    std::string endian_str = hr.attribute("endian").as_string("BIG");
                    reg_data.big_endian = (endian_str != "LITTLE");
                    if (reg_data.datatype.find("INT32") != std::string::npos ||
                        reg_data.datatype.find("UINT32") != std::string::npos) {
                        reg_data.register_count = 2;
                    }

                    // 存储到映射和字典中
                    this->fc03_nameToAddr_map[name] = reg_data.address;
                    this->data_dict_[name] = reg_data;
                    this->dev_data_keys_.push_back(name);
                }
            }
        }

        // 解析功能码04（输入寄存器）
        pugi::xml_node fc04_node = root.child("function_code04");
        if (fc04_node) {
            for (pugi::xml_node ir : fc04_node.children("iRegister")) {
                std::string name = ir.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(ir.attribute("address").as_string()));
                    reg_data.mag =  static_cast<double>(std::stoi(ir.attribute("mag").as_string()));
                    reg_data.offset =  static_cast<uint16_t>(std::stoi(ir.attribute("offset").as_string()));
                    reg_data.datatype = ir.attribute("datatype").as_string();
                    reg_data.unit = ir.attribute("unit").as_string();
                    reg_data.value = 0.0;
                    std::string endian_str = ir.attribute("endian").as_string("BIG");
                    reg_data.big_endian = (endian_str != "LITTLE");
                    if (reg_data.datatype.find("INT32") != std::string::npos ||
                        reg_data.datatype.find("UINT32") != std::string::npos) {
                        reg_data.register_count = 2;
                    }

                    // 存储到映射和字典中
                    this->fc04_nameToAddr_map[name] = reg_data.address;
                    this->data_dict_[name] = reg_data;
                    this->dev_data_keys_.push_back(name);
                }
            }
        }

        pugi::xml_node dido_node = root.child("dido");
        if (dido_node) { 
            parse_alarm_config(dido_node);
        }

        // 预留数据缓存区空间
        this->data_buffer.reserve(150);

        // 初始化数据数组大小，与寄存器数量一致
        this->data_to_qt["data"] = json::array();
        for (size_t i = 0; i < this->data_dict_.size(); ++i) {
            this->data_to_qt["data"].push_back(0.0);
        }
        LOG_INFO_LOC(("设备 " + this->name_ + "初始化寄存器成功, 共: " + std::to_string(this->data_dict_.size()) + " 个寄存器.").c_str());
    }

    // 解析告警信息 - 子类可选择性调用
    virtual void parse_alarm_config(pugi::xml_node& dido_node) {
        for (pugi::xml_node subdido : dido_node.children("subdido")) {
            std::string name = subdido.attribute("name").as_string();
            int level = subdido.attribute("level").as_int(1);

            if (!name.empty()) {
                // 添加到总数据JSON中
                this->data_to_qt[name] = false;
                json alarm_obj = json::object();
                    alarm_obj["value"] = false;
                    alarm_obj["lastTriggerTime"] = "";
                    alarm_obj["lastClearTime"] = "";
                // 根据级别初始化对应的告警JSON对象
                switch (level) {
                    case 1: this->alarm_level1[name] = alarm_obj; break;
                    case 2: this->alarm_level2[name] = alarm_obj; break;
                    case 3: this->alarm_level3[name] = alarm_obj; break;
                }
                // 存储告警名称和级别的映射关系
                this->alarm_map.push_back({name, level});
            }
        }
        LOG_INFO_LOC(("设备 " + name_ + "初始化告警成功: " + std::to_string(this->alarm_map.size()) + " alarms.").c_str());
    }

    // 写单个线圈或寄存器 (功能码 05 或 06)
    virtual bool writeCmdToDevice(std::shared_ptr<ModbusClient> mb_client, 
                                 const std::string& function_code,
                                 const uint16_t& addr,
                                 const uint16_t& value) {
        if (!mb_client || !mb_client->is_connected()) {
            LOG_WARNING_LOC(("Modbus client is not connected for device: " + name_).c_str());
            return false;
        }
        
        // 设置从站地址
        if (!mb_client->set_slave(id_)) {
            LOG_ERROR_LOC(("Failed to set slave address for device: " + name_).c_str());
            return false;
        }
        
        bool result = false;
        
        if (function_code == "05") {
            // 写单个线圈 (线圈值应为0或1)
            bool coil_value = (value != 0);
            result = mb_client->write_coil(addr, coil_value);
            if (result) {
                LOG_INFO_LOC(("Device " + name_ + ": Write coil at address " + std::to_string(addr) + " with value " + std::to_string(coil_value) + " success").c_str());
            } else {
                LOG_ERROR_LOC(("Device " + name_ + ": Write coil at address " + std::to_string(addr) + " failed").c_str());
            }
        } 
        else if (function_code == "06") {
            // 写单个寄存器
            result = mb_client->write_register(addr, value);
            if (result) {
                LOG_INFO_LOC(("Device " + name_ + ": Write register at address " + std::to_string(addr) + " with value " + std::to_string(value) + " success").c_str());
            } else {
                LOG_ERROR_LOC(("Device " + name_ + ": Write register at address " + std::to_string(addr) + " failed").c_str());
            }
        }
        else {
            LOG_ERROR_LOC(("Device " + name_ + ": Unsupported function code for single write: " + function_code).c_str());
            return false;
        }
        
        return result;
    }

   // 写多个线圈或寄存器 (功能码 15 或 16)
    virtual bool multiWriteCmdToDevice(std::shared_ptr<ModbusClient> mb_client,
                                      const std::string& function_code,
                                      const uint16_t& addr,
                                      const std::vector<uint16_t>& value_list) {
        if (!mb_client || !mb_client->is_connected()) {
            LOG_WARNING_LOC(("Modbus client is not connected for device: " + this->name_).c_str());
            return false;
        }
        
        // 设置从站地址
        if (!mb_client->set_slave(this->id_)) {
            LOG_ERROR_LOC(("Failed to set slave address for device: " + this->name_).c_str());
            return false;
        }
        
        if (value_list.empty()) {
            LOG_WARNING_LOC(("Device " + this->name_ + ": Value list is empty").c_str());
            return false;
        }
        
        bool result = false;
        
        if (function_code == "15") {
            // 写多个线圈
            // 将uint16_t转换为uint8_t数组
            std::vector<uint8_t> coil_values(value_list.size());
            for (size_t i = 0; i < value_list.size(); ++i) {
                coil_values[i] = (value_list[i] != 0) ? 1 : 0;
            }
            
            result = mb_client->write_coils(addr, static_cast<int>(value_list.size()), coil_values.data());
            if (result) {
                LOG_INFO_LOC(("Device " + name_ + ": Write " + std::to_string(value_list.size()) + " coils starting at address " + std::to_string(addr) + " success").c_str());
            } else {
                LOG_ERROR_LOC(("Device " + name_ + ": Write coils starting at address " + std::to_string(addr) + " failed").c_str());
            }
        } 
        else if (function_code == "16") {
            // 写多个寄存器
            result = mb_client->write_registers(addr, static_cast<int>(value_list.size()), value_list.data());
            if (result) {
                LOG_INFO_LOC(("Device " + name_ + ": Write " + std::to_string(value_list.size()) + " registers starting at address " + std::to_string(addr) + " success").c_str());
            } else {
                LOG_ERROR_LOC(("Device " + name_ + ": Write registers starting at address " + std::to_string(addr) + " failed").c_str());
            }
        }
        else {
            LOG_ERROR_LOC(("Device " + name_ + ": Unsupported function code for multiple write: " + function_code).c_str());
            return false;
        }
        
        return result;
    }

    /**
     * @brief 将 uint16 展开为 16 位 bool 数组 (bit 15 → index 0, bit 0 → index 15)
     * 与 Python utils.uint16_to_switches 行为完全一致
     * @param value 16 位无符号整数
     * @return 长度为 16 的 bool 数组，高位在前
     */
    static std::vector<bool> uint16_to_switches(uint16_t value) {
        std::vector<bool> result(16);
        for (int i = 15; i >= 0; --i) {
            result[15 - i] = (value >> i) & 0x01;
        }
        return result;
    }

    virtual bool writeCanFrameToDevice(std::shared_ptr<CanOperator> can_client,
                                       sockcanpp::CanId can_id,
                                       const std::vector<uint8_t>& payload) {
        if (!can_client || !can_client->is_connected()) {
            LOG_WARNING_LOC(("CAN client is not connected for device: " + name_).c_str());
            return false;
        }

        const bool result = can_client->send_frame(can_id, payload);
        if (!result) {
            LOG_ERROR_LOC(("Device " + name_ + ": Send CAN frame failed, can_id=" +
                           std::to_string(static_cast<uint32_t>(can_id))).c_str());
        }
        return result;
    }

    /**
     * @brief 通过引用发送 CAN 帧（重载，与 read_data 签名风格一致）
     */
    virtual bool writeCanFrameToDevice(CanOperator& can_client,
                                       sockcanpp::CanId can_id,
                                       const std::vector<uint8_t>& payload) {
        if (!can_client.is_connected()) {
            LOG_WARNING_LOC(("CAN client is not connected for device: " + name_).c_str());
            return false;
        }
        const bool result = can_client.send_frame(can_id, payload);
        if (!result) {
            LOG_ERROR_LOC(("Device " + name_ + ": Send CAN frame failed, can_id=" +
                           std::to_string(static_cast<uint32_t>(can_id))).c_str());
        }
        return result;
    }

    inline const uint8_t get_com() const { return com_; }

    inline const uint8_t get_id() const { return id_; }

    inline const std::string& get_name() const { return name_; }

    // bool online_status;
    std::atomic<bool> online_status{false};

    std::vector<std::string> dev_data_keys_;

    std::vector<uint16_t> data_buffer;  // 缓存从设备读取的数据

    // 段读取策略
    std::vector<RegisterSegment> segments01_;
    std::vector<RegisterSegment> segments02_;
    std::vector<RegisterSegment> segments03_;
    std::vector<RegisterSegment> segments04_;

    std::vector<std::vector<uint8_t>> data_buffer_vec01_;
    std::vector<std::vector<uint8_t>> data_buffer_vec02_;
    std::vector<std::vector<uint16_t>> data_buffer_vec03_;
    std::vector<std::vector<uint16_t>> data_buffer_vec04_;

    // 字段名->每个功能码的数组的地址映射
    std::vector<ParsedRegister> parsed_registers_fc01_;
    std::vector<ParsedRegister> parsed_registers_fc02_;
    std::vector<ParsedRegister> parsed_registers_fc03_;
    std::vector<ParsedRegister> parsed_registers_fc04_;

    // 字段名->每个功能码的modbus原始地址映射
    std::unordered_map<std::string, uint16_t> fc01_nameToAddr_map;
    std::unordered_map<std::string, uint16_t> fc02_nameToAddr_map;
    std::unordered_map<std::string, uint16_t> fc03_nameToAddr_map;
    std::unordered_map<std::string, uint16_t> fc04_nameToAddr_map;

    // 内部数据处理使用的map
    std::unordered_map<std::string, RegisterData> data_dict_;

    // 字段名->总的数组的地址映射
    std::vector<ParsedRegister> parsed_registers_;
    std::vector<std::pair<std::string, uint8_t>> alarm_map;     // 报警名称:等级的vector

    json data_to_qt;
    
    // 读写锁，保护 data_to_qt 的并发访问（C++17）
    // 注意：如果派生类有自己的锁（如 EMS 的 json_rwlock_），应在派生类中统一保护
    mutable std::shared_mutex data_to_qt_rwlock_;

    mutable std::shared_mutex data_dict_rwlock_;  // 新增data_dict锁

    // 线程安全的data_dict访问方法
    template<typename T>
    void setValue(const std::string& key, T value) {
        std::unique_lock<std::shared_mutex> lock(data_dict_rwlock_);
        auto it = data_dict_.find(key);
        if (it != data_dict_.end()) {
            it->second.value = value;
        }
    }
    
    template<typename T>
    T getValue(const std::string& key, T default_value = T{}) {
        std::shared_lock<std::shared_mutex> lock(data_dict_rwlock_);
        auto it = data_dict_.find(key);
        if (it != data_dict_.end()) {
            try {
                return static_cast<T>(it->second.value);
            } catch(...) {
                return default_value;
            }
        }
        return default_value;
    }

    /**
     * @brief 线程安全地更新寄存器的value字段
     * @param key 寄存器名称（键）
     * @param value 新的数值
     * @return true 如果更新成功，false 如果键不存在
     * @note 此方法仅更新 RegisterData 的 value 字段，保持 mag/offset 等配置不变
     */
    bool updateRegisterValue(const std::string& key, double value) {
        std::unique_lock<std::shared_mutex> lock(data_dict_rwlock_);
        auto it = data_dict_.find(key);
        if (it != data_dict_.end()) {
            it->second.value = value;
            return true;
        }
        LOG_WARNING_LOC(("设备 " + this->name_ + ": 寄存器 " + key + "未找到，无法更新值").c_str());
        return false;
    }

    /**
     * @brief 线程安全地获取寄存器的配置信息（mag和offset）
     * @param key 寄存器名称（键）
     * @param out_mag 输出的缩放系数
     * @param out_offset 输出的偏移量
     * @param out_datatype 输出的数据类型
     * @return true 如果获取成功，false 如果键不存在
     * @note 使用共享锁，支持并发读取配置
     */
    bool getRegisterConfig(const std::string& key, double& out_mag, uint16_t& out_offset, std::string& out_datatype) {
        std::shared_lock<std::shared_mutex> lock(data_dict_rwlock_);
        auto it = data_dict_.find(key);
        if (it != data_dict_.end()) {
            out_mag = it->second.mag;
            out_offset = it->second.offset;
            out_datatype = it->second.datatype;
            return true;
        }
        return false;
    }

    bool getRegisterConfig(const std::string& key, double& out_mag, uint16_t& out_offset,
                           std::string& out_datatype, bool& out_big_endian, uint8_t& out_register_count) {
        std::shared_lock<std::shared_mutex> lock(data_dict_rwlock_);
        auto it = data_dict_.find(key);
        if (it != data_dict_.end()) {
            out_mag = it->second.mag;
            out_offset = it->second.offset;
            out_datatype = it->second.datatype;
            out_big_endian = it->second.big_endian;
            out_register_count = it->second.register_count;
            return true;
        }
        return false;
    }

    
    /**
     * @brief 线程安全地获取 data_to_qt 的副本
     * @return data_to_qt 的深拷贝，调用者可以安全使用
     * @note 如果派生类有专用锁，应重写此方法使用专用锁
     */
    virtual json get_data_to_qt_safe() const {
        std::shared_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        return this->data_to_qt;  // 返回副本
    }
    
    /**
     * @brief 线程安全地更新 data_to_qt
     * @param new_data 新的 JSON 数据
     * @note 如果派生类有专用锁，应重写此方法使用专用锁
     */
    virtual void set_data_to_qt_safe(const json& new_data) {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt = new_data;
    }

    /**
     * @brief 通用告警处理方法（参考 Python device.py 的 handle_alarm）
     * @param alarm_name 告警名称
     * @param level 告警级别 (1/2/3)
     * @param status 当前告警状态 (true=触发, false=恢复)
     * @param debounce_ms 去抖时间（毫秒），告警值需持续该时间后才确认生效，默认100ms；设为0或负数则跳过去抖直接生效
     * @note 此方法会自动处理告警触发/恢复的数据库记录和缓存更新
     * @note 派生类可以重写此方法以实现自定义告警逻辑
     */
    virtual void handle_alarm(const std::string& alarm_name, 
                             uint8_t level, 
                             bool status, 
                             int debounce_ms = 100);

    /**
     * @brief 确认告警状态变更（去抖定时器到期后调用）
     * @param alarm_name 告警名称
     * @param level 告警级别 (1/2/3)
     * @param status 确认后的告警状态
     */
    void _confirm_alarm(const std::string& alarm_name,
                        uint8_t level,
                        bool status);




    virtual void safe_set_qt_data(const bool online){
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["online_status"] = online;
        this->data_to_qt["timestamp"] = ss.str();
    }
    
    virtual void safe_set_qt_data(const bool online,const json& json_array){
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["online_status"] = online;
        if (online) 
            this->data_to_qt["data"] = json_array;
        this->data_to_qt["timestamp"] = ss.str();
    }

    json alarm_level1;
    json alarm_level2;
    json alarm_level3;

    std::string name_;
    uint8_t com_;
    uint8_t id_;

    uint16_t reconnect_counter = 0; // 重连次数计数器，用于设置设备离线状态

    // 告警状态缓存，用于检测告警状态变化
    std::unordered_map<std::string, bool> alarm_cached;

    // 告警去抖定时器 {alarm_name: AlarmDebounceTimer}
    std::unordered_map<std::string, std::shared_ptr<AlarmDebounceTimer>> alarm_timers_;
    // 告警待确认值 {alarm_name: pending_alarm_value}
    std::unordered_map<std::string, bool> alarm_pending_values_;
    // 保护 alarm_timers_ 和 alarm_pending_values_ 的互斥锁
    std::mutex alarm_timer_mtx_;

    /// 3-arg version: fills out_parsed with LOCAL buffer indices (relative to the given segments).
    /// Does NOT touch parsed_registers_ — use build_parsed_registers() afterwards if needed.
    void init_useful_indexes_from_map(
        const std::unordered_map<std::string, uint16_t>& name_to_addr_map,
        const std::vector<RegisterSegment>& segments,
        std::vector<ParsedRegister>& out_parsed) {
        out_parsed.clear();

        std::unordered_map<uint16_t, uint16_t> addr_to_buffer_index;
        uint16_t buffer_idx = 0;
        for (const auto& seg : segments) {
            for (uint16_t addr = seg.start_addr; addr < seg.start_addr + seg.num_regs; ++addr) {
                addr_to_buffer_index[addr] = buffer_idx;
                ++buffer_idx;
            }
        }

        for (const auto& key : dev_data_keys_) {
            auto addr_it = name_to_addr_map.find(key);
            if (addr_it == name_to_addr_map.end()) continue;
            auto buf_it = addr_to_buffer_index.find(addr_it->second);
            if (buf_it != addr_to_buffer_index.end()) {
                out_parsed.push_back({key, buf_it->second});
            } else {
                LOG_WARNING_LOC("地址 " + std::to_string(addr_it->second) +
                    " (寄存器: " + key + ") 不在任何读取段中");
            }
        }

        if (out_parsed.size() != name_to_addr_map.size()) {
            LOG_WARNING_LOC("索引数量(" + std::to_string(out_parsed.size()) +
                ") 与寄存器数量(" + std::to_string(name_to_addr_map.size()) + ")不匹配");
        } else {
            LOG_INFO_LOC("索引匹配成功: " + std::to_string(out_parsed.size()) + " 个寄存器已索引");
        }
    }

    /// 2-arg backward-compatible wrapper: fills parsed_registers_ directly.
    /// Used by single-FC devices — no changes needed in existing code.
    void init_useful_indexes_from_map(
        const std::unordered_map<std::string, uint16_t>& name_to_addr_map,
        const std::vector<RegisterSegment>& segments) {
        init_useful_indexes_from_map(name_to_addr_map, segments, parsed_registers_);
    }

    /// Helper: count total registers in a vector of segments
    static uint16_t count_segment_registers(const std::vector<RegisterSegment>& segs) {
        uint16_t total = 0;
        for (const auto& seg : segs) total += seg.num_regs;
        return total;
    }

    /// Build parsed_registers_ from all per-FC vectors with correct global buffer offsets.
    /// The offset for each FC is the total register count of all preceding FCs,
    /// matching the concatenation order in read_all_registers().
    void build_parsed_registers() {
        parsed_registers_.clear();
        uint16_t offset = 0;

        auto append_with_offset = [this](const std::vector<ParsedRegister>& fc_vec,
                                          uint16_t& offset,
                                          const std::vector<RegisterSegment>& segs) {
            for (const auto& pr : fc_vec) {
                parsed_registers_.push_back(
                    {pr.key, static_cast<uint16_t>(pr.buffer_index + offset)});
            }
            offset += count_segment_registers(segs);
        };

        append_with_offset(parsed_registers_fc01_, offset, segments01_);
        append_with_offset(parsed_registers_fc02_, offset, segments02_);
        append_with_offset(parsed_registers_fc03_, offset, segments03_);
        append_with_offset(parsed_registers_fc04_, offset, segments04_);

        LOG_INFO_LOC("构建全局寄存器索引完成: " + std::to_string(parsed_registers_.size()) +
                     " 个寄存器, 总偏移 " + std::to_string(offset));
    }

    static double parse_register_value(
        const std::vector<uint16_t>& data_list,
        uint16_t buffer_index,
        double mag,
        uint16_t offset,
        const std::string& datatype,
        bool big_endian = true,
        uint8_t register_count = 1) {
        if (buffer_index >= data_list.size()) return 0.0;

        double actual_value = 0.0;

        if (datatype.find("BOOL") != std::string::npos) {
            actual_value = (data_list[buffer_index] != 0) ? 1.0 : 0.0;
        } else if (register_count == 2 && buffer_index + 1 < data_list.size()) {
            Utils::Endian endian = big_endian ? Utils::Endian::BIG : Utils::Endian::LITTLE;
            if (datatype.find("INT32") != std::string::npos) {
                uint32_t raw = Utils::getUint32num(data_list[buffer_index], data_list[buffer_index + 1], endian);
                actual_value = static_cast<double>(static_cast<int32_t>(raw)) / mag + offset;
            } else {
                uint32_t raw = Utils::getUint32num(data_list[buffer_index], data_list[buffer_index + 1], endian);
                actual_value = static_cast<double>(raw) / mag + offset;
            }
        } else if (datatype.find("INT16") != std::string::npos) {
            actual_value = static_cast<double>(static_cast<int16_t>(data_list[buffer_index])) / mag + offset;
        } else {
            actual_value = static_cast<double>(data_list[buffer_index]) / mag + offset;
        }

        return actual_value;
    }


    // 通用解析原始数据，适用于单个功能码的设备
    void parse_rawdata_generic(const std::vector<uint16_t>& data_list) {
        this->online_status = true;
        json data_array = json::array();

        for (const auto& pr : this->parsed_registers_) {
            if (pr.buffer_index >= data_list.size()) break;

            double mag = 1.0;
            uint16_t offset = 0;
            std::string datatype;
            bool big_endian = true;
            uint8_t register_count = 1;

            if (!this->getRegisterConfig(pr.key, mag, offset, datatype, big_endian, register_count)) {
                data_array.push_back(0.0);
                continue;
            }

            double actual_value = parse_register_value(
                data_list, pr.buffer_index, mag, offset, datatype, big_endian, register_count);

            this->updateRegisterValue(pr.key, actual_value);
            data_array.push_back(actual_value);
        }

        safe_set_qt_data(true, data_array);

    }

    bool read_fc01_segments(ModbusClient& mb_client,
                            const std::vector<RegisterSegment>& segments,
                            std::vector<std::vector<uint8_t>>& buffer_vec,
                            std::vector<uint16_t>& data_buffer) {
        if (!mb_client.is_connected()) return false;

        mb_client.set_slave(this->id_);

        buffer_vec.resize(segments.size());
        for (size_t i = 0; i < segments.size(); ++i) {
            buffer_vec[i].resize(segments[i].num_regs);
            if (!mb_client.read_coils(
                    segments[i].start_addr, segments[i].num_regs, buffer_vec[i].data())) {
                return false;
            }
        }

        for (const auto& buf : buffer_vec) {
            data_buffer.insert(data_buffer.end(), buf.begin(), buf.end());
        }
        return true;
    }

    bool read_fc02_segments(ModbusClient& mb_client,
                            const std::vector<RegisterSegment>& segments,
                            std::vector<std::vector<uint8_t>>& buffer_vec,
                            std::vector<uint16_t>& data_buffer) {
        if (!mb_client.is_connected()) return false;

        mb_client.set_slave(this->id_);

        buffer_vec.resize(segments.size());
        for (size_t i = 0; i < segments.size(); ++i) {
            buffer_vec[i].resize(segments[i].num_regs);
            if (!mb_client.read_input_bits(
                    segments[i].start_addr, segments[i].num_regs, buffer_vec[i].data())) {
                return false;
            }
        }

        for (const auto& buf : buffer_vec) {
            data_buffer.insert(data_buffer.end(), buf.begin(), buf.end());
        }
        return true;
    }
   

    bool read_fc03_segments(ModbusClient& mb_client,
                            const std::vector<RegisterSegment>& segments,
                            std::vector<std::vector<uint16_t>>& buffer_vec,
                            std::vector<uint16_t>& data_buffer) {
        if (!mb_client.is_connected()) return false;

        mb_client.set_slave(this->id_);

        LOG_DEBUG_F("FC03(%s id=%d): reading %zu segments", this->name_.c_str(), this->id_, segments.size());

        buffer_vec.resize(segments.size());
        for (size_t i = 0; i < segments.size(); ++i) {
            buffer_vec[i].resize(segments[i].num_regs);
            LOG_DEBUG_F("FC03(%s id=%d): seg[%zu] start=%u count=%u (addr %u-%u)",
                         this->name_.c_str(), this->id_, i,
                         segments[i].start_addr, segments[i].num_regs,
                         segments[i].start_addr,
                         segments[i].start_addr + segments[i].num_regs - 1);
            if (!mb_client.read_holding_registers(
                    segments[i].start_addr, segments[i].num_regs, buffer_vec[i].data())) {
                LOG_ERROR_F("FC03(%s id=%d): seg[%zu] FAILED start=%u count=%u (addr %u-%u)",
                            this->name_.c_str(), this->id_, i,
                            segments[i].start_addr, segments[i].num_regs,
                            segments[i].start_addr,
                            segments[i].start_addr + segments[i].num_regs - 1);
                return false;
            }
        }

        for (const auto& buf : buffer_vec) {
            data_buffer.insert(data_buffer.end(), buf.begin(), buf.end());
        }
        return true;
    }



    bool read_fc04_segments(ModbusClient& mb_client,
                            const std::vector<RegisterSegment>& segments,
                            std::vector<std::vector<uint16_t>>& buffer_vec,
                            std::vector<uint16_t>& data_buffer) {
        if (!mb_client.is_connected()) return false;

        mb_client.set_slave(this->id_);

        buffer_vec.resize(segments.size());
        for (size_t i = 0; i < segments.size(); ++i) {
            buffer_vec[i].resize(segments[i].num_regs);
            if (!mb_client.read_input_registers(
                    segments[i].start_addr, segments[i].num_regs, buffer_vec[i].data())) {
                return false;
            }
        }

        for (const auto& buf : buffer_vec) {
            data_buffer.insert(data_buffer.end(), buf.begin(), buf.end());
        }
        return true;
    }

    bool read_all_registers(ModbusClient& mb_client){
        this->data_buffer.clear();
        bool success = true;

        LOG_DEBUG_F("read_all_registers(%s id=%d): FC01=%zu FC02=%zu FC03=%zu FC04=%zu segments",
                    this->name_.c_str(), this->id_,
                    this->segments01_.size(),
                    this->segments02_.size(),
                    this->segments03_.size(),
                    this->segments04_.size());

        if (!this->fc01_nameToAddr_map.empty())
            success = read_fc01_segments(mb_client, this->segments01_, this->data_buffer_vec01_, this->data_buffer);
        if (!this->fc02_nameToAddr_map.empty())
            success =read_fc02_segments(mb_client, this->segments02_, this->data_buffer_vec02_, this->data_buffer);
        if (!this->fc03_nameToAddr_map.empty())
            success =read_fc03_segments(mb_client, this->segments03_, this->data_buffer_vec03_, this->data_buffer);
        if (!this->fc04_nameToAddr_map.empty())
            success =read_fc04_segments(mb_client, this->segments04_, this->data_buffer_vec04_, this->data_buffer);
        return success;
    }

    static std::vector<RegisterSegment> generate_segments_from_addresses(
        const std::vector<uint16_t>& addresses,
        uint16_t max_registers_per_segment = 100,
        uint16_t gap_tolerance = 5) {
        std::vector<RegisterSegment> segments;
        if (addresses.empty()) return segments;

        std::vector<uint16_t> sorted_addrs = addresses;
        std::sort(sorted_addrs.begin(), sorted_addrs.end());
        sorted_addrs.erase(std::unique(sorted_addrs.begin(), sorted_addrs.end()), sorted_addrs.end());

        uint16_t segment_start = sorted_addrs[0];
        uint16_t segment_end = sorted_addrs[0];

        auto flush_segment = [&]() {
            uint16_t num_regs = segment_end - segment_start + 1;
            if (num_regs > 0) {
                segments.push_back({segment_start, num_regs});
            }
        };

        for (size_t i = 1; i < sorted_addrs.size(); ++i) {
            uint16_t addr = sorted_addrs[i];
            uint16_t gap = addr - segment_end;
            uint16_t current_count = segment_end - segment_start + 1;

            if (gap == 1) {
                segment_end = addr;
                if (current_count + 1 >= max_registers_per_segment) {
                    flush_segment();
                    segment_start = addr;
                    segment_end = addr;
                }
            } else if (gap > 1 && gap <= gap_tolerance &&
                       current_count + gap <= max_registers_per_segment) {
                segment_end = addr;
            } else {
                flush_segment();
                segment_start = addr;
                segment_end = addr;
            }
        }

        flush_segment();
        return segments;
    }
};

#endif // DEVICE_H