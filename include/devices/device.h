#ifndef DEVICE_H
#define DEVICE_H

#include <iostream>
#include <memory>
#include <vector>
#include <shared_mutex>  // C++17 读写锁支持
#include "json.hpp"
#include "canoperator.h"
#include "modbusclient.h"
#include <pugixml.hpp>
#include "log.h"

using json = nlohmann::json;


struct RegisterData {
    uint16_t address = 0;
    double value = 0.0;
    double mag = 1.0;
    uint16_t offset = 0;
    std::string datatype;
    std::string unit;
};

class Device {
public:
    struct RegisterSegment {
        uint16_t start_addr;
        uint16_t num_regs;
    };

    Device(const std::string& name, int com, int id)
        : name_(name), com_(com), id_(id) {}

    virtual ~Device() = default;

    virtual void parse_rawdata(const std::vector<uint16_t>& data_list)=0;

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
        LOG_INFO_LOC(("Loading device config from: " + config_file).c_str());
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

        // 解析功能码01（离散输出）
        pugi::xml_node fc01_node = root.child("function_code01");
        if (fc01_node) {
            for (pugi::xml_node hr : fc01_node.children("hRegister")) {
                std::string name = hr.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(hr.attribute("address").as_string()));
                    reg_data.mag =  static_cast<double>(std::stoi(hr.attribute("mag").as_string()));
                    reg_data.offset =  static_cast<uint16_t>(std::stoi(hr.attribute("offset").as_string()));
                    reg_data.datatype = hr.attribute("datatype").as_string();
                    reg_data.unit = hr.attribute("unit").as_string();
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
            for (pugi::xml_node hr : fc02_node.children("hRegister")) {
                std::string name = hr.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(hr.attribute("address").as_string()));
                    reg_data.mag =  static_cast<double>(std::stoi(hr.attribute("mag").as_string()));
                    reg_data.offset =  static_cast<uint16_t>(std::stoi(hr.attribute("offset").as_string()));
                    reg_data.datatype = hr.attribute("datatype").as_string();
                    reg_data.unit = hr.attribute("unit").as_string();
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
            for (pugi::xml_node hr : fc04_node.children("hRegister")) {
                std::string name = hr.attribute("name").as_string();
                if (!name.empty()) {
                    RegisterData reg_data;
                    reg_data.address = static_cast<uint16_t>(std::stoi(hr.attribute("address").as_string()));
                    reg_data.mag =  static_cast<double>(std::stoi(hr.attribute("mag").as_string()));
                    reg_data.offset =  static_cast<uint16_t>(std::stoi(hr.attribute("offset").as_string()));
                    reg_data.datatype = hr.attribute("datatype").as_string();
                    reg_data.unit = hr.attribute("unit").as_string();
                    reg_data.value = 0.0;

                    // 存储到映射和字典中
                    this->fc04_nameToAddr_map[name] = reg_data.address;
                    this->data_dict_[name] = reg_data;
                    this->dev_data_keys_.push_back(name);
                }
            }
        }

        // 预留数据缓存区空间
        this->data_buffer.reserve(150);

        // 初始化数据数组大小，与寄存器数量一致
        this->data_to_qt["data"] = json::array();
        for (size_t i = 0; i < this->data_dict_.size(); ++i) {
            this->data_to_qt["data"].push_back(0.0);
        }
        LOG_INFO_LOC(("Device config loaded successfully: " + std::to_string(this->data_dict_.size()) + " registers.").c_str());
    }

    // 解析告警信息 - 子类可选择性调用
    virtual void parse_alarm_config(pugi::xml_node& root) {
        pugi::xml_node dido_node = root.child("dido");
        if (dido_node) {
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
        }
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
            LOG_WARNING_LOC(("Modbus client is not connected for device: " + name_).c_str());
            return false;
        }
        
        // 设置从站地址
        if (!mb_client->set_slave(this->id_)) {
            LOG_ERROR_LOC(("Failed to set slave address for device: " + name_).c_str());
            return false;
        }
        
        if (value_list.empty()) {
            LOG_WARNING_LOC(("Device " + name_ + ": Value list is empty").c_str());
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

    inline const uint8_t get_com() const { return com_; }

    inline const uint8_t get_id() const { return id_; }

    inline const std::string& get_name() const { return name_; }

    // bool online_status;
    std::atomic<bool> online_status{false};

    std::vector<std::string> dev_data_keys_;

    std::vector<uint16_t> data_buffer;  // 缓存从设备读取的数据

    std::unordered_map<std::string, uint16_t> fc01_nameToAddr_map;
    std::unordered_map<std::string, uint16_t> fc02_nameToAddr_map;
    std::unordered_map<std::string, uint16_t> fc03_nameToAddr_map;
    std::unordered_map<std::string, uint16_t> fc04_nameToAddr_map;

    // 内部数据处理使用的map
    std::unordered_map<std::string, RegisterData> data_dict_;

    std::vector<uint16_t> useful_indexes;
    std::vector<std::pair<std::string, uint8_t>> alarm_map;

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
     * @param now 当前时间字符串
     * @note 此方法会自动处理告警触发/恢复的数据库记录和缓存更新
     * @note 派生类可以重写此方法以实现自定义告警逻辑
     */
    virtual void handle_alarm(const std::string& alarm_name, 
                             uint8_t level, 
                             bool status, 
                             const std::string& now);

    json alarm_level1;
    json alarm_level2;
    json alarm_level3;

    std::string name_;
    uint8_t com_;
    uint8_t id_;

    uint16_t reconnect_counter = 0; // 重连次数计数器，用于设置设备离线状态

    // 告警状态缓存，用于检测告警状态变化
    std::unordered_map<std::string, bool> alarm_cached;

    static std::vector<RegisterSegment> generate_segments_from_addresses(
        const std::vector<uint16_t>& addresses, 
        uint16_t max_registers_per_segment = 100) {
        std::vector<RegisterSegment> segments;
        if (addresses.empty()) return segments;

        std::vector<uint16_t> sorted_addrs = addresses;
        std::sort(sorted_addrs.begin(), sorted_addrs.end());

        uint16_t segment_start = sorted_addrs[0];
        uint16_t segment_end = sorted_addrs[0];

        for (size_t i = 1; i < sorted_addrs.size(); ++i) {
            uint16_t addr = sorted_addrs[i];
            if (addr == segment_end + 1) {
                segment_end = addr;
            } else {
                if (segment_end >= segment_start) {
                    uint16_t num_regs = segment_end - segment_start + 1;
                    if (num_regs > 0) {
                        segments.push_back({segment_start, num_regs});
                    }
                }
                segment_start = addr;
                segment_end = addr;
            }
        }

        if (segment_end >= segment_start) {
            uint16_t num_regs = segment_end - segment_start + 1;
            if (num_regs > 0) {
                segments.push_back({segment_start, num_regs});
            }
        }

        return segments;
    }
};

#endif // DEVICE_H