#include "device.h"
#include "devicemanager.h"
#include "strategy.h"
#include "unixsocketserver.h"
#include "config.h"
#include "log.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>

static std::atomic<bool> keep_running{true};
static std::atomic<int> signal_received{0};

// 改进的信号处理函数
void signal_handler(int signum) {
    static int signal_count = 0;
    signal_count++;
    
    if (signal_count == 1) {
        keep_running = false;
        signal_received = signum;
    } else if (signal_count >= 2) {
        exit(1);
    }
}

// 设置信号处理器
void setup_signal_handler() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}


// C++ 风格的时间格式化
std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto timeinfo = *std::localtime(&time_t_now);
    
    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%H:%M:%S");
    return oss.str();
}

int main() {
    // 确保日志位置信息启用
    LOG_INFO_LOC("程序启动，日志系统初始化完成");
    
    std::cout << "按 Ctrl+C 退出程序\n" ;
    std::cout << "（按一次Ctrl+C优雅停止，快速按两次强制退出）" << "\n";
    setup_signal_handler();

    try {
        std::shared_ptr<DeviceManager> device_manager = std::make_shared<DeviceManager>();  // 创建设备管理器实例
        device_manager->createReadThreads();        // 启动设备数据读取线程
        device_manager->startRunningLogThread();    // 启动运行日志线程
        device_manager->startModbusTcpServer();     // 启动 Modbus TCP 服务器

        // 启动云端控制订阅（与Python端保持一致）
        device_manager->startSubscribeCloudControl();
        
        // 设置控制消息回调（可选）
        device_manager->setControlMessageCallback([](const std::string& channel, const std::string& message) {
            LOG_INFO_LOC(("自定义控制消息处理: channel=" + channel).c_str());
            // 这里可以添加自定义的控制逻辑
        });
        
        // 启动Socket服务器（与qt通讯）
        std::unique_ptr<UnixSocketServer> socket_server = std::make_unique<UnixSocketServer>(Config::SOCKET_PATH, device_manager->devices_);
        socket_server->startServer();
        // 启动策略线程
        std::unique_ptr<Strategy> strategy = std::make_unique<Strategy>(device_manager);
        strategy->runningThread();


        
        // 主循环，检查是否收到停止信号
        while (keep_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            
            // 在停止标志设置后,不再执行数据发布和设备更新
            if (!keep_running.load()) {
                break;
            }
            
            device_manager->publishDataToRedis();
            std::dynamic_pointer_cast<EMS>(device_manager->getDeviceByName("ems"))->update_ems();
            
            // 可以在这里添加其他定期检查或状态汇报
            static int counter = 0;
            if (++counter % 100 == 0) { // 每1秒打印一次
                std::cout << "程序运行中..." << "\n";
            }
        }
        
        std::cout << "正在停止所有线程..." << "\n";
        
        // 先停止策略线程
        strategy->stopThread();

        // 停止Socket服务器
        socket_server->stopServer();

        // 停止 Modbus TCP 服务器
        device_manager->stopModbusTcpServer();

        // 最后停止设备管理器的所有线程(包括日志线程和云端订阅线程)
        device_manager->stopAllThreads();
        
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n" << "程序正常退出。" << "\n";
    return 0;
}


/**
 * 设备数据保存到数据库的简单示例
 * 
 * 这个示例展示了如何直接将设备的 data_dict_ 保存到数据库
 */

// // 【选项1】使用 SqlDatabase (SQLite3 C API)
// #include "sqldatabase.h"

// // 【选项2】使用 SqlCpp (SQLiteCpp C++ API) - 注释掉上面，启用下面这行
// // #include "sqlcpp.h"

// #include "device.h"
// #include <iostream>

// void example_save_device_data() {
//     std::cout << "=== 设备数据保存示例 ===" << std::endl;
    
//     // 1. 初始化数据库
//     if (!SQL_DB.initialize("main.db", "log.db", "alarm.db")) {
//         std::cerr << "数据库初始化失败: " << SQL_DB.getLastError() << std::endl;
//         return;
//     }
    
//     // 2. 模拟一个设备的 data_dict_（就像 PCS 类中的那样）
//     std::unordered_map<std::string, RegisterData> device_data;
    
//     // 添加电压数据
//     RegisterData voltage;
//     voltage.address = 40001;
//     voltage.value = 220.5;
//     voltage.mag = 10;
//     voltage.offset = 0;
//     voltage.datatype = "UINT16";
//     voltage.unit = "V";
//     device_data["voltage"] = voltage;
    
//     // 添加电流数据
//     RegisterData current;
//     current.address = 40002;
//     current.value = 10.2;
//     current.mag = 100;
//     current.offset = 0;
//     current.datatype = "UINT16";
//     current.unit = "A";
//     device_data["current"] = current;
    
//     // 添加功率数据
//     RegisterData power;
//     power.address = 40003;
//     power.value = 2249.1;
//     power.mag = 1;
//     power.offset = 0;
//     power.datatype = "INT32";
//     power.unit = "W";
//     device_data["power"] = power;
    
//     // 添加状态数据
//     RegisterData status;
//     status.address = 40005;
//     status.value = 1;
//     status.mag = 1;
//     status.offset = 0;
//     status.datatype = "UINT16";
//     status.unit = "";
//     device_data["status"] = status;
    
//     // 3. 【关键】直接插入设备数据，无需任何转换！
//     std::string table_name = "pcs_001";  // 使用设备名称作为表名
    
//     if (SQL_DB.insertDeviceDataFromRegister(table_name, device_data, true)) {
//         std::cout << "✓ 设备数据保存成功！" << std::endl;
//         std::cout << "  表名: " << table_name << std::endl;
//         std::cout << "  字段数: " << device_data.size() << std::endl;
        
//         // 打印保存的数据
//         for (const auto& pair : device_data) {
//             std::cout << "    " << pair.first << " = " << pair.second.value 
//                       << " " << pair.second.unit << std::endl;
//         }
//     } else {
//         std::cerr << "✗ 保存失败: " << SQL_DB.getLastError() << std::endl;
//     }
    
//     // 4. 批量插入示例
//     std::cout << "\n=== 批量插入示例 ===" << std::endl;
    
//     std::vector<std::unordered_map<std::string, RegisterData>> records;
    
//     // 模拟10条历史记录
//     for (int i = 0; i < 10; ++i) {
//         std::unordered_map<std::string, RegisterData> record;
        
//         RegisterData v;
//         v.value = 220.0 + i * 0.5;
//         v.datatype = "FLOAT";
//         record["voltage"] = v;
        
//         RegisterData c;
//         c.value = 10.0 + i * 0.2;
//         c.datatype = "FLOAT";
//         record["current"] = c;
        
//         records.push_back(record);
//     }
    
//     if (SQL_DB.batchInsertDeviceDataFromRegister("pcs_history", records, true)) {
//         std::cout << "✓ 批量插入成功！共 " << records.size() << " 条记录" << std::endl;
//     } else {
//         std::cerr << "✗ 批量插入失败: " << SQL_DB.getLastError() << std::endl;
//     }
    
//     // 5. 查询最新数据
//     std::cout << "\n=== 查询最新数据 ===" << std::endl;
    
//     auto latest = SQL_DB.queryLatestDeviceData(table_name);
//     if (!latest.empty()) {
//         std::cout << "✓ 查询到最新数据：" << std::endl;
//         for (const auto& pair : latest) {
//             std::cout << "  " << pair.first << " = ";
//             switch (pair.second.type) {
//                 case FieldType::INTEGER:
//                     std::cout << pair.second.value.int_val;
//                     break;
//                 case FieldType::FLOAT:
//                     std::cout << pair.second.value.float_val;
//                     break;
//                 default:
//                     std::cout << "(unknown)";
//                     break;
//             }
//             std::cout << std::endl;
//         }
//     }
    
//     // 6. 关闭数据库
//     SQL_DB.close();
    
//     std::cout << "\n=== 示例完成 ===" << std::endl;
// }

// int main() {
//     example_save_device_data();
//     return 0;
// }
