/**
 * 设备数据保存到数据库的简单示例
 * 
 * 这个示例展示了如何直接将设备的 data_dict_ 保存到数据库
 */

#include "sqldatabase.h"  // 或 #include "sqlcpp.h"
#include "device.h"
#include <iostream>

void example_save_device_data() {
    std::cout << "=== 设备数据保存示例 ===" << std::endl;
    
    // 1. 初始化数据库
    if (!SQL_DB.initialize("main.db", "log.db", "alarm.db")) {
        std::cerr << "数据库初始化失败: " << SQL_DB.getLastError() << std::endl;
        return;
    }
    
    // 2. 模拟一个设备的 data_dict_（就像 PCS 类中的那样）
    std::unordered_map<std::string, RegisterData> device_data;
    
    // 添加电压数据
    RegisterData voltage;
    voltage.address = 40001;
    voltage.value = 220.5;
    voltage.mag = 10;
    voltage.offset = 0;
    voltage.datatype = "UINT16";
    voltage.unit = "V";
    device_data["voltage"] = voltage;
    
    // 添加电流数据
    RegisterData current;
    current.address = 40002;
    current.value = 10.2;
    current.mag = 100;
    current.offset = 0;
    current.datatype = "UINT16";
    current.unit = "A";
    device_data["current"] = current;
    
    // 添加功率数据
    RegisterData power;
    power.address = 40003;
    power.value = 2249.1;
    power.mag = 1;
    power.offset = 0;
    power.datatype = "INT32";
    power.unit = "W";
    device_data["power"] = power;
    
    // 添加状态数据
    RegisterData status;
    status.address = 40005;
    status.value = 1;
    status.mag = 1;
    status.offset = 0;
    status.datatype = "UINT16";
    status.unit = "";
    device_data["status"] = status;
    
    // 3. 【关键】直接插入设备数据，无需任何转换！
    std::string table_name = "pcs_001";  // 使用设备名称作为表名
    
    if (SQL_DB.insertDeviceDataFromRegister(table_name, device_data, true)) {
        std::cout << "✓ 设备数据保存成功！" << std::endl;
        std::cout << "  表名: " << table_name << std::endl;
        std::cout << "  字段数: " << device_data.size() << std::endl;
        
        // 打印保存的数据
        for (const auto& pair : device_data) {
            std::cout << "    " << pair.first << " = " << pair.second.value 
                      << " " << pair.second.unit << std::endl;
        }
    } else {
        std::cerr << "✗ 保存失败: " << SQL_DB.getLastError() << std::endl;
    }
    
    // 4. 批量插入示例
    std::cout << "\n=== 批量插入示例 ===" << std::endl;
    
    std::vector<std::unordered_map<std::string, RegisterData>> records;
    
    // 模拟10条历史记录
    for (int i = 0; i < 10; ++i) {
        std::unordered_map<std::string, RegisterData> record;
        
        RegisterData v;
        v.value = 220.0 + i * 0.5;
        v.datatype = "FLOAT";
        record["voltage"] = v;
        
        RegisterData c;
        c.value = 10.0 + i * 0.2;
        c.datatype = "FLOAT";
        record["current"] = c;
        
        records.push_back(record);
    }
    
    if (SQL_DB.batchInsertDeviceDataFromRegister("pcs_history", records, true)) {
        std::cout << "✓ 批量插入成功！共 " << records.size() << " 条记录" << std::endl;
    } else {
        std::cerr << "✗ 批量插入失败: " << SQL_DB.getLastError() << std::endl;
    }
    
    // 5. 查询最新数据
    std::cout << "\n=== 查询最新数据 ===" << std::endl;
    
    auto latest = SQL_DB.queryLatestDeviceData(table_name);
    if (!latest.empty()) {
        std::cout << "✓ 查询到最新数据：" << std::endl;
        for (const auto& pair : latest) {
            std::cout << "  " << pair.first << " = ";
            switch (pair.second.type) {
                case FieldType::INTEGER:
                    std::cout << pair.second.value.int_val;
                    break;
                case FieldType::FLOAT:
                    std::cout << pair.second.value.float_val;
                    break;
                default:
                    std::cout << "(unknown)";
                    break;
            }
            std::cout << std::endl;
        }
    }
    
    // 6. 关闭数据库
    SQL_DB.close();
    
    std::cout << "\n=== 示例完成 ===" << std::endl;
}

// 在 PCS 类中使用的简化示例
void example_in_pcs_class() {
    std::cout << "\n=== 在 PCS 类中使用 ===" << std::endl;
    
    // 假设这是 PCS::parse_rawdata() 方法的一部分
    // this->data_dict_ 已经填充好了数据
    
    // 只需一行代码即可保存！
    if (SQL_DB.isInitialized()) {
        SQL_DB.insertDeviceDataFromRegister(this->name_, this->data_dict_, this->online_status);
    }
    
    std::cout << "✓ PCS 数据已保存到数据库" << std::endl;
}

int main() {
    example_save_device_data();
    return 0;
}
