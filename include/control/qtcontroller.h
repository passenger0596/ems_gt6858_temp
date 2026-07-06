#ifndef QT_CONTROLLER_H
#define QT_CONTROLLER_H

#include "json.hpp"
#include <memory>
#include <mutex>
#include "config.h"

using json = nlohmann::json;
class QtController {
    public:
        QtController(const std::string& cfg_file_path);
        ~QtController();
        static std::shared_ptr<QtController> instance_;
        json cmd_from_qt;  // 存储从QT接收的命令
        mutable std::mutex cmd_mutex_;  // 保护 cmd_from_qt 并发写入
        void parse_qt_data(const json& qtData);
        static std::shared_ptr<QtController> getInstance() {
            if (!instance_) {
                instance_ = std::make_shared<QtController>(Config::CONTROL_CMD_FILEPATH);
            }
            return instance_;
        }
        QtController(const QtController&) = delete; // 禁止拷贝构造，确保单例模式的唯一性
        QtController& operator=(const QtController&) = delete; // 禁止赋值运算符，确保单例模式的唯一性
    private:


};

#endif