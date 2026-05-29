#include "qtcontroller.h"
#include <fstream>
#include "log.h"

// 定义静态成员变量
std::shared_ptr<QtController> QtController::instance_ = nullptr;



QtController::QtController(const std::string& cfg_file_path) {
    // 可以在这里加载配置文件，初始化成员变量等
    std::fstream ifs(cfg_file_path);
    if (!ifs.is_open()) {
        LOG_ERROR_LOC("Failed to open config file: " + cfg_file_path);
        return;
    }
    ifs >> this->cmd_from_qt;
    ifs.close();
    LOG_INFO_LOC("cmd_from_qt init done!");
    
}

QtController::~QtController() {
    // 析构函数实现
    LOG_INFO_LOC("QtController destroyed");
}



void QtController::parse_qt_data(const json& qtData) {
    try{
        if(qtData.contains("device")&&qtData.contains("type")){
            if (qtData["type"] == "single"){
                this->cmd_from_qt[qtData["device"]][qtData["command"]] = qtData["value"];
                LOG_DEBUG_LOC("parse_qt_data: " + qtData["device"].get<std::string>() + ", " + 
                             qtData["command"].get<std::string>() + ", " + qtData["value"].dump());
                if (qtData["command"] == "sys_setTimer")
                    this->cmd_from_qt[qtData["device"]]["timingModeSet"] = qtData["timingModeSet"];
                if (qtData["command"] == "sys_setDemandResponse")
                    this->cmd_from_qt[qtData["device"]]["demandResponseModeSet"] = qtData["demandResponseModeSet"];
                
            }
            if (qtData["type"] == "multi"){
                if (qtData["value"].is_array()){
                    this->cmd_from_qt[qtData["device"]][qtData["command"]] = true;
                    for (const auto& item : qtData["value"]){
                        for (const auto&[key,value] : item.items()){
                            this->cmd_from_qt[qtData["device"]][key] = value;
                        }
                    }
                }
            }
        }
    }catch(const std::exception& e){
        LOG_ERROR_LOC("Error parsing QT data: " + std::string(e.what()));
    }
}