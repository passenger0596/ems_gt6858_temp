#include "sqlcpp.h"
#include "log.h"
#include <iostream>
#include <chrono>
#include <thread>

/**
 * SQLiteCpp数据库操作示例
 * 
 * 这个示例展示了如何使用SqlCpp类进行数据库操作
 * SqlCpp提供了与SqlDatabase相同的功能，但使用SQLiteCpp库实现
 */

void testOperationLog() {
    std::cout << "\n=== 测试操作日志 ===" << std::endl;
    
    // 创建操作日志
    OperationLog log1("2024-01-01 10:00:00", "INFO", "System", "系统启动");
    OperationLog log2("2024-01-01 10:01:00", "WARNING", "Device", "设备离线");
    
    // 插入单条日志
    if (SQL_CPP.insertOperationLog(log1)) {
        std::cout << "✓ 操作日志插入成功" << std::endl;
    } else {
        std::cout << "✗ 操作日志插入失败: " << SQL_CPP.getLastError() << std::endl;
    }
    
    // 批量插入日志
    std::vector<OperationLog> logs = {log1, log2};
    if (SQL_CPP.batchInsertOperationLogs(logs)) {
        std::cout << "✓ 批量插入操作日志成功" << std::endl;
    } else {
        std::cout << "✗ 批量插入操作日志失败: " << SQL_CPP.getLastError() << std::endl;
    }
}

void testAlarmHistory() {
    std::cout << "\n=== 测试告警历史 ===" << std::endl;
    
    // 创建告警记录
    AlarmHistory alarm("严重", "2024-01-01 10:00:00", "PCS001", "温度过高", "NA");
    
    // 插入告警记录
    if (SQL_CPP.insertAlarmHistory(alarm)) {
        std::cout << "✓ 告警记录插入成功" << std::endl;
    } else {
        std::cout << "✗ 告警记录插入失败: " << SQL_CPP.getLastError() << std::endl;
    }
    
    // 更新恢复时间
    if (SQL_CPP.updateAlarmRecoveryTime("2024-01-01 10:00:00", "PCS001", "温度过高")) {
        std::cout << "✓ 告警恢复时间更新成功" << std::endl;
    } else {
        std::cout << "✗ 告警恢复时间更新失败: " << SQL_CPP.getLastError() << std::endl;
    }
    
    // 查询未恢复的告警
    auto unrecovered = SQL_CPP.queryUnrecoveredAlarms();
    std::cout << "✓ 查询到 " << unrecovered.size() << " 条未恢复告警" << std::endl;
}

void testDeviceData() {
    std::cout << "\n=== 测试设备数据 ===" << std::endl;
    
    std::string table_name = "test_device";
    
    // 定义表字段
    std::map<std::string, FieldInfo> fields;
    fields["voltage"] = FieldInfo("voltage", FieldType::FLOAT, 10, false);
    fields["current"] = FieldInfo("current", FieldType::FLOAT, 10, false);
    fields["power"] = FieldInfo("power", FieldType::FLOAT, 10, false);
    fields["status"] = FieldInfo("status", FieldType::INTEGER, 10, false);
    
    // 创建设备表
    if (SQL_CPP.createDeviceTable(table_name, fields)) {
        std::cout << "✓ 设备表创建成功" << std::endl;
    } else {
        std::cout << "✗ 设备表创建失败: " << SQL_CPP.getLastError() << std::endl;
        return;
    }
    
    // 检查并更新表结构
    if (SQL_CPP.checkAndUpdateTableStructure(table_name, fields)) {
        std::cout << "✓ 表结构检查完成" << std::endl;
    } else {
        std::cout << "✗ 表结构检查失败: " << SQL_CPP.getLastError() << std::endl;
    }
    
    // 插入设备数据
    std::map<std::string, DeviceDataItem> data;
    data["voltage"] = DeviceDataItem("voltage", 220.5);
    data["current"] = DeviceDataItem("current", 10.2);
    data["power"] = DeviceDataItem("power", 2249.1);
    data["status"] = DeviceDataItem("status", static_cast<int64_t>(1));
    
    if (SQL_CPP.insertDeviceData(table_name, data, true)) {
        std::cout << "✓ 设备数据插入成功" << std::endl;
    } else {
        std::cout << "✗ 设备数据插入失败: " << SQL_CPP.getLastError() << std::endl;
    }
    
    // 批量插入数据
    std::vector<std::map<std::string, DeviceDataItem>> records;
    for (int i = 0; i < 5; ++i) {
        std::map<std::string, DeviceDataItem> record;
        record["voltage"] = DeviceDataItem("voltage", 220.0 + i * 0.1);
        record["current"] = DeviceDataItem("current", 10.0 + i * 0.2);
        record["power"] = DeviceDataItem("power", 2200.0 + i * 20.0);
        record["status"] = DeviceDataItem("status", static_cast<int64_t>(1));
        records.push_back(record);
    }
    
    if (SQL_CPP.batchInsertDeviceData(table_name, records, true)) {
        std::cout << "✓ 批量插入设备数据成功" << std::endl;
    } else {
        std::cout << "✗ 批量插入设备数据失败: " << SQL_CPP.getLastError() << std::endl;
    }
    
    // 查询最新数据
    auto latest = SQL_CPP.queryLatestDeviceData(table_name);
    std::cout << "✓ 查询到最新数据，共 " << latest.size() << " 个字段" << std::endl;
    
    // 查询历史数据
    auto history = SQL_CPP.queryDeviceHistory(table_name, 
                                              "2024-01-01 00:00:00",
                                              "2024-12-31 23:59:59",
                                              10);
    std::cout << "✓ 查询到 " << history.size() << " 条历史记录" << std::endl;
}

void testCustomQuery() {
    std::cout << "\n=== 测试自定义查询 ===" << std::endl;
    
    std::string sql = "SELECT name FROM sqlite_master WHERE type='table';";
    
    bool success = SQL_CPP.executeQuery(sql, [](int column_count, char** values, char** columns) -> int {
        for (int i = 0; i < column_count; ++i) {
            std::cout << "  " << columns[i] << ": " << (values[i] ? values[i] : "NULL") << std::endl;
        }
        return 0; // 继续下一行
    });
    
    if (success) {
        std::cout << "✓ 自定义查询执行成功" << std::endl;
    } else {
        std::cout << "✗ 自定义查询执行失败: " << SQL_CPP.getLastError() << std::endl;
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  SQLiteCpp 数据库操作示例" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 初始化数据库
    std::cout << "\n正在初始化数据库..." << std::endl;
    if (!SQL_CPP.initialize("test_main.db", "test_log.db", "test_alarm.db")) {
        std::cerr << "数据库初始化失败: " << SQL_CPP.getLastError() << std::endl;
        return 1;
    }
    std::cout << "✓ 数据库初始化成功" << std::endl;
    
    // 运行测试
    testOperationLog();
    testAlarmHistory();
    testDeviceData();
    testCustomQuery();
    
    // 关闭数据库
    std::cout << "\n正在关闭数据库..." << std::endl;
    SQL_CPP.close();
    std::cout << "✓ 数据库已关闭" << std::endl;
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "  所有测试完成" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}
