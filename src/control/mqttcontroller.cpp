#include "mqttcontroller.h"
#include <fstream>
#include "log.h"

// 定义静态成员变量
std::shared_ptr<MqttController> MqttController::instance_ = nullptr;

MqttController::MqttController(const std::string& cfg_file_path) {
    // 加载 MQTT 控制命令配置文件
    std::ifstream ifs(cfg_file_path);
    if (!ifs.is_open()) {
        LOG_ERROR_LOC("Failed to open MQTT config file: " + cfg_file_path);
        return;
    }
    
    try {
        ifs >> this->control_dict_;
        ifs.close();
        LOG_INFO_LOC("MQTT control_dict initialized from: " + cfg_file_path);
        LOG_DEBUG_LOC(("Loaded " + std::to_string(control_dict_.size()) + " MQTT commands").c_str());
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Failed to parse MQTT config file: " + std::string(e.what()));
        ifs.close();
    }
}

MqttController::~MqttController() {
    LOG_INFO_LOC("MqttController destroyed");
}

void MqttController::parse_mqtt_data(const json& mqttData) {
    try {
        // 检查是否包含 cmd_id 字段
        if (mqttData.contains("cmd_id")) {
            std::string address = mqttData["cmd_id"].get<std::string>();
            
            // 检查 cmd_id 是否在控制字典中
            if (control_dict_.contains(address)) {
                // 更新对应地址的值
                if (mqttData.contains("value")) {
                    control_dict_[address] = mqttData["value"];
                    LOG_DEBUG_LOC(("parse_mqtt_data success: cmd_id=" + address + 
                                 ", value=" + mqttData["value"].dump()).c_str());
                } else {
                    LOG_WARNING_LOC(("parse_mqtt_data warning: cmd_id=" + address + 
                                   " has no value field").c_str());
                }
            } else {
                LOG_ERROR_LOC(("parse_mqtt_data error: cmd_id " + address + " not in control_dict").c_str());
            }
        } else {
            LOG_WARNING_LOC("parse_mqtt_data warning: mqttData has no cmd_id field");
        }
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("parse_mqtt_data error: " + std::string(e.what())).c_str());
    }
}