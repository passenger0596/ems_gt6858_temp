#include "sqldatabase.h"
#include "log.h"
#include <iostream>
#include <thread>
#include <chrono>

// 示例1：操作日志
void example_operation_log() {
    LOG_INFO_LOC("=== 示例1：操作日志 ===");
    
    // 插入单条操作日志
    OperationLog log1(
        SqlDatabase::getInstance().getCurrentTimeString(),
        "CONFIG_CHANGE",
        "WEB_UI",
        "修改了PCS功率限制参数"
    );
    
    if (SQL_DB.insertOperationLog(log1)) {
        LOG_INFO_LOC("操作日志插入成功");
    } else {
        LOG_ERROR_LOC(("操作日志插入失败: " + SQL_DB.getLastError()).c_str());
    }
    
    // 批量插入操作日志
    std::vector<OperationLog> logs;
    for (int i = 0; i < 5; ++i) {
        logs.emplace_back(
            SqlDatabase::getInstance().getCurrentTimeString(),
            "DEVICE_EVENT",
            "SYSTEM",
            "设备状态检查 #" + std::to_string(i)
        );
    }
    
    if (SQL_DB.batchInsertOperationLogs(logs)) {
        LOG_INFO_LOC("批量操作日志插入成功");
    }
}

// 示例2：告警历史
void example_alarm_history() {
    LOG_INFO_LOC("=== 示例2：告警历史 ===");
    
    // 插入告警记录
    AlarmHistory alarm1(
        "WARNING",
        SqlDatabase::getInstance().getCurrentTimeString(),
        "PCS_01",
        "温度过高警告"
    );
    
    if (SQL_DB.insertAlarmHistory(alarm1)) {
        LOG_INFO_LOC("告警记录插入成功");
    }
    
    // 模拟一段时间后更新恢复时间
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    if (SQL_DB.updateAlarmRecoveryTime(
            alarm1.alarm_time,
            alarm1.device_name,
            alarm1.description)) {
        LOG_INFO_LOC("告警恢复时间更新成功");
    }
    
    // 查询未恢复的告警
    auto unrecovered = SQL_DB.queryUnrecoveredAlarms();
    LOG_INFO_LOC(("未恢复告警数量: " + std::to_string(unrecovered.size())).c_str());
    
    for (const auto& alarm : unrecovered) {
        LOG_DEBUG_LOC(("告警: " + alarm.level + " - " + alarm.device_name + 
                       " - " + alarm.description).c_str());
    }
}

// 示例3：设备数据
void example_device_data() {
    LOG_INFO_LOC("=== 示例3：设备数据 ===");
    
    // 定义DCDC设备的字段
    std::map<std::string, FieldInfo> dcdc_fields;
    dcdc_fields["voltage"] = FieldInfo("voltage", FieldType::FLOAT, 10, true, false);
    dcdc_fields["current"] = FieldInfo("current", FieldType::FLOAT, 10, true, false);
    dcdc_fields["power"] = FieldInfo("power", FieldType::FLOAT, 10, true, false);
    dcdc_fields["temperature"] = FieldInfo("temperature", FieldType::FLOAT, 10, true, false);
    dcdc_fields["status"] = FieldInfo("status", FieldType::INTEGER, 0, true, false);
    
    // 创建设备表
    if (SQL_DB.createDeviceTable("dcdc_01", dcdc_fields)) {
        LOG_INFO_LOC("设备表创建成功");
    }
    
    // 插入单条设备数据
    std::map<std::string, DeviceDataItem> data1;
    data1["voltage"] = DeviceDataItem("voltage", 48.5);
    data1["current"] = DeviceDataItem("current", 12.3);
    data1["power"] = DeviceDataItem("power", 596.55);
    data1["temperature"] = DeviceDataItem("temperature", 45.2);
    data1["status"] = DeviceDataItem("status", static_cast<int64_t>(1));
    
    if (SQL_DB.insertDeviceData("dcdc_01", data1, true)) {
        LOG_INFO_LOC("设备数据插入成功");
    }
    
    // 批量插入设备数据
    std::vector<std::map<std::string, DeviceDataItem>> batch_data;
    for (int i = 0; i < 10; ++i) {
        std::map<std::string, DeviceDataItem> record;
        record["voltage"] = DeviceDataItem("voltage", 48.0 + i * 0.1);
        record["current"] = DeviceDataItem("current", 12.0 + i * 0.2);
        record["power"] = DeviceDataItem("power", 576.0 + i * 5.0);
        record["temperature"] = DeviceDataItem("temperature", 45.0 + i * 0.5);
        record["status"] = DeviceDataItem("status", static_cast<int64_t>(1));
        batch_data.push_back(record);
    }
    
    if (SQL_DB.batchInsertDeviceData("dcdc_01", batch_data, true)) {
        LOG_INFO_LOC("批量设备数据插入成功");
    }
    
    // 查询最新设备数据
    auto latest = SQL_DB.queryLatestDeviceData("dcdc_01");
    LOG_INFO_LOC(("最新数据字段数: " + std::to_string(latest.size())).c_str());
    
    for (const auto& [field, item] : latest) {
        switch (item.type) {
            case FieldType::FLOAT:
                LOG_DEBUG_LOC((field + ": " + std::to_string(item.value.float_val)).c_str());
                break;
            case FieldType::INTEGER:
                LOG_DEBUG_LOC((field + ": " + std::to_string(item.value.int_val)).c_str());
                break;
            default:
                break;
        }
    }
    
    // 查询历史数据
    auto now = SqlDatabase::getInstance().getCurrentTimeString();
    auto history = SQL_DB.queryDeviceHistory("dcdc_01", "2024-01-01 00:00:00", now, 5);
    LOG_INFO_LOC(("历史记录数量: " + std::to_string(history.size())).c_str());
}

int main() {
    LOG_INFO_LOC("SQLite数据库示例程序启动");
    
    // 初始化数据库
    if (!SQL_DB.initialize()) {
        LOG_ERROR_LOC("数据库初始化失败");
        return -1;
    }
    
    try {
        // 运行示例
        example_operation_log();
        std::cout << "\n";
        
        example_alarm_history();
        std::cout << "\n";
        
        example_device_data();
        std::cout << "\n";
        
        LOG_INFO_LOC("所有示例执行完成");
    } catch (const std::exception& e) {
        LOG_ERROR_LOC(("异常: " + std::string(e.what())).c_str());
    }
    
    // 关闭数据库
    SQL_DB.close();
    
    return 0;
}
