#ifndef MQTT_CONTROLLER_H
#define MQTT_CONTROLLER_H

#include "json.hpp"
#include <memory>
#include <string>
#include "config.h"

using json = nlohmann::json;

class MqttController {
public:
    ~MqttController();
    
    // 单例模式获取实例
    static std::shared_ptr<MqttController> getInstance() {
        if (!instance_) {
            instance_.reset(new MqttController(Config::MQTT_CMD_FILEPATH));
        }
        return instance_;
    }
    
    // 禁止拷贝和赋值
    MqttController(const MqttController&) = delete;
    MqttController& operator=(const MqttController&) = delete;
    
    // 解析 MQTT 数据
    void parse_mqtt_data(const json& mqttData);
    
    // 获取控制字典（只读访问）
    const json& get_control_dict() const { return control_dict_; }
    
private:
    MqttController(const std::string& cfg_file_path);
    
    static std::shared_ptr<MqttController> instance_;
    json control_dict_;  // 存储 MQTT 控制命令字典
};

#endif // MQTT_CONTROLLER_H
