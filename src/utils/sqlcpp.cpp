#include "sqlcpp.h"
#include "device.h"  // 包含RegisterData定义
#include "log.h"
#include <sstream>
#include <algorithm>
#include <ctime>
#include <cstring>
#include <iomanip>

// ==================== 单例实现 ====================

SqlCpp& SqlCpp::getInstance() {
    static SqlCpp instance;
    return instance;
}

SqlCpp::SqlCpp() 
    : initialized_(false) {
    // 在构造函数中自动初始化数据库
    if (!initialize()) {
        LOG_ERROR_LOC("数据库自动初始化失败，请检查数据库路径和权限");
        // 注意：这里不抛出异常，允许程序继续运行（降级处理）
    }
}

SqlCpp::~SqlCpp() {
    close();
}

// ==================== 初始化与关闭 ====================

bool SqlCpp::initialize(const std::string& main_db_path,
                        const std::string& log_db_path,
                        const std::string& alarm_db_path) {
    if (initialized_) {
        LOG_WARNING_LOC("数据库已初始化");
        return true;
    }
    
    // 打开主数据库（设备数据）
    if (!openDatabase(main_db_, main_db_path)) {
        last_error_ = "无法打开主数据库: " + main_db_path;
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    // 创建操作日志表
    if (!openDatabase(log_db_, log_db_path)) {
        last_error_ = "无法打开日志数据库: " + log_db_path;
        LOG_ERROR_LOC(last_error_.c_str());
        closeDatabase(main_db_);
        return false;
    }
    
    try {
        const char* create_log_table_sql = 
            "CREATE TABLE IF NOT EXISTS operation ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "type TEXT NOT NULL,"
            "source TEXT NOT NULL,"
            "desc TEXT NOT NULL"
            ");";
        
        log_db_->exec(create_log_table_sql);
    } catch (const std::exception& e) {
        last_error_ = "创建操作日志表失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        close();
        return false;
    }
    
    // 创建告警历史表
    if (!openDatabase(alarm_db_, alarm_db_path)) {
        last_error_ = "无法打开告警数据库: " + alarm_db_path;
        LOG_ERROR_LOC(last_error_.c_str());
        closeDatabase(main_db_);
        closeDatabase(log_db_);
        return false;
    }
    
    try {
        const char* create_alarm_table_sql = 
            "CREATE TABLE IF NOT EXISTS alarmHistory ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "等级 TEXT,"
            "告警时间 TEXT DEFAULT (datetime('now', 'localtime')),"
            "设备名称 TEXT,"
            "告警描述 TEXT,"
            "恢复时间 TEXT DEFAULT 'NA'"
            ");";
        
        alarm_db_->exec(create_alarm_table_sql);
    } catch (const std::exception& e) {
        last_error_ = "创建告警历史表失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        close();
        return false;
    }
    
    initialized_ = true;
    LOG_INFO_LOC("数据库初始化成功");
    return true;
}

void SqlCpp::close() {
    std::lock_guard<std::mutex> lock_main(main_mutex_);
    std::lock_guard<std::mutex> lock_log(log_mutex_);
    std::lock_guard<std::mutex> lock_alarm(alarm_mutex_);
    
    closeDatabase(main_db_);
    closeDatabase(log_db_);
    closeDatabase(alarm_db_);
    
    initialized_ = false;
    LOG_INFO_LOC("数据库连接已关闭");
}

bool SqlCpp::openDatabase(std::unique_ptr<SQLite::Database>& db, const std::string& path) {
    try {
        db = std::make_unique<SQLite::Database>(path, 
            SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        
        // 启用WAL模式以提高并发性能
        db->exec("PRAGMA journal_mode=WAL;");
        
        // 设置busy timeout为30秒
        db->exec("PRAGMA busy_timeout=30000;");
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = "无法打开数据库: " + path + ", 错误: " + e.what();
        LOG_ERROR_LOC(last_error_.c_str());
        db.reset();
        return false;
    }
}

void SqlCpp::closeDatabase(std::unique_ptr<SQLite::Database>& db) {
    db.reset();
}

// ==================== 操作日志相关 ====================

bool SqlCpp::insertOperationLog(const OperationLog& log) {
    if (!initialized_ || !log_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    try {
        SQLite::Statement query(*log_db_, 
            "INSERT INTO operation (timestamp, type, source, desc) VALUES (?, ?, ?, ?);");
        
        query.bind(1, log.timestamp);
        query.bind(2, log.type);
        query.bind(3, log.source);
        query.bind(4, log.desc);
        
        query.exec();
        
        LOG_DEBUG_LOC(("操作日志插入成功: " + log.desc).c_str());
        return true;
    } catch (const std::exception& e) {
        last_error_ = "插入操作日志失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::batchInsertOperationLogs(const std::vector<OperationLog>& logs) {
    if (!initialized_ || !log_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    try {
        // 开始事务
        SQLite::Transaction transaction(*log_db_);
        
        SQLite::Statement query(*log_db_, 
            "INSERT INTO operation (timestamp, type, source, desc) VALUES (?, ?, ?, ?);");
        
        int success_count = 0;
        for (const auto& log : logs) {
            try {
                query.bind(1, log.timestamp);
                query.bind(2, log.type);
                query.bind(3, log.source);
                query.bind(4, log.desc);
                
                query.exec();
                query.reset();
                success_count++;
            } catch (const std::exception& e) {
                LOG_WARNING_LOC(("插入单条日志失败: " + std::string(e.what())).c_str());
                query.reset();
                continue;
            }
        }
        
        // 提交事务
        transaction.commit();
        
        LOG_INFO_LOC(("批量插入操作日志完成: " + std::to_string(success_count) + "/" + 
                      std::to_string(logs.size())).c_str());
        return success_count > 0;
    } catch (const std::exception& e) {
        last_error_ = "批量插入操作日志失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

// ==================== 告警历史相关 ====================

bool SqlCpp::insertAlarmHistory(const AlarmHistory& alarm) {
    if (!initialized_ || !alarm_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarm_mutex_);
    
    try {
        SQLite::Statement query(*alarm_db_, 
            "INSERT INTO alarmHistory (等级, 告警时间, 设备名称, 告警描述, 恢复时间) "
            "VALUES (?, ?, ?, ?, ?);");
        
        query.bind(1, alarm.level);
        query.bind(2, alarm.alarm_time);
        query.bind(3, alarm.device_name);
        query.bind(4, alarm.description);
        query.bind(5, alarm.recovery_time);
        
        query.exec();
        
        LOG_INFO_LOC(("告警记录插入成功: " + alarm.level + ":" + alarm.device_name + ":" + alarm.description).c_str());
        return true;
    } catch (const std::exception& e) {
        last_error_ = "插入告警记录失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::updateAlarmRecoveryTime(const std::string& alarm_time,
                                     const std::string& device_name,
                                     const std::string& description,
                                     const std::string& recovery_time) {
    if (!initialized_ || !alarm_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarm_mutex_);
    
    std::string final_recovery_time = recovery_time.empty() ? getCurrentTimeString() : recovery_time;
    
    try {
        // ✅ 使用 SQLite 支持的子查询方式：查找最新的未恢复告警记录的 ID
        SQLite::Statement query(*alarm_db_, 
            "UPDATE alarmHistory SET 恢复时间 = ? "
            "WHERE id = ("
            "    SELECT id FROM alarmHistory "
            "    WHERE 设备名称 = ? AND 告警描述 = ? AND 恢复时间 = 'NA' "
            "    ORDER BY 告警时间 DESC LIMIT 1"
            ");");
        
        query.bind(1, final_recovery_time);
        query.bind(2, device_name);
        query.bind(3, description);
        
        int rows_affected = query.exec();
        
        if (rows_affected == 0) {
            LOG_WARNING_LOC(("未找到匹配的未恢复告警记录: " + device_name + ":" + description).c_str());
            last_error_ = "未找到匹配的未恢复告警记录";
            return false;
        }
        
        LOG_INFO_LOC(("成功更新告警恢复时间: " + device_name + ":" + description + " (影响行数: " + std::to_string(rows_affected) + ")").c_str());
        return true;
    } catch (const std::exception& e) {
        last_error_ = "更新告警恢复时间失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

std::vector<AlarmHistory> SqlCpp::queryUnrecoveredAlarms() {
    std::vector<AlarmHistory> result;
    
    if (!initialized_ || !alarm_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    std::lock_guard<std::mutex> lock(alarm_mutex_);
    
    try {
        SQLite::Statement query(*alarm_db_, 
            "SELECT id, 等级, 告警时间, 设备名称, 告警描述, 恢复时间 "
            "FROM alarmHistory WHERE 恢复时间 = 'NA' OR 恢复时间 IS NULL;");
        
        while (query.executeStep()) {
            AlarmHistory alarm;
            alarm.id = query.getColumn(0).getInt();
            alarm.level = query.getColumn(1).getText();
            alarm.alarm_time = query.getColumn(2).getText();
            alarm.device_name = query.getColumn(3).getText();
            alarm.description = query.getColumn(4).getText();
            alarm.recovery_time = query.getColumn(5).getText();
            
            result.push_back(alarm);
        }
        
        LOG_DEBUG_LOC(("查询到 " + std::to_string(result.size()) + " 条未恢复告警").c_str());
    } catch (const std::exception& e) {
        last_error_ = "查询未恢复告警失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
    }
    
    return result;
}

// ==================== 设备数据相关 ====================

bool SqlCpp::createDeviceTable(const std::string& table_name,
                               const std::map<std::string, FieldInfo>& fields) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        std::string escaped_table = escapeSqlIdentifier(table_name);
        std::stringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << escaped_table << " ("
            << "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            << "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
            << "online_status BOOLEAN DEFAULT 0";
        
        for (const auto& field : fields) {
            sql << ", " << escapeSqlIdentifier(field.first) << " " 
                << fieldTypeToSql(field.second.type, field.second.length);
            
            if (!field.second.nullable) {
                sql << " NOT NULL";
            }
            if (field.second.unique) {
                sql << " UNIQUE";
            }
        }
        
        sql << ");";
        
        main_db_->exec(sql.str());
        
        LOG_INFO_LOC(("设备表创建成功: " + table_name).c_str());
        return true;
    } catch (const std::exception& e) {
        last_error_ = "创建设备表失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::checkAndUpdateTableStructure(const std::string& table_name,
                                          const std::map<std::string, FieldInfo>& expected_fields) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        // 检查表是否存在
        SQLite::Statement check_query(*main_db_, 
            "SELECT count(*) FROM sqlite_master WHERE type='table' AND name=?;");
        check_query.bind(1, table_name);
        
        bool table_exists = false;
        if (check_query.executeStep()) {
            table_exists = check_query.getColumn(0).getInt() > 0;
        }
        
        if (!table_exists) {
            last_error_ = "表不存在: " + table_name;
            LOG_ERROR_LOC(last_error_.c_str());
            return false;
        }
        
        // 获取现有字段
        std::string pragma_sql = "PRAGMA table_info(" + escapeSqlIdentifier(table_name) + ");";
        std::map<std::string, std::string> existing_columns;
        
        SQLite::Statement pragma_query(*main_db_, pragma_sql);
        while (pragma_query.executeStep()) {
            std::string col_name = pragma_query.getColumn(1).getText();
            existing_columns[col_name] = "";
        }
        
        // 找出缺失的字段
        std::vector<std::string> missing_fields;
        for (const auto& field : expected_fields) {
            if (existing_columns.find(field.first) == existing_columns.end()) {
                missing_fields.push_back(field.first);
            }
        }
        
        if (missing_fields.empty()) {
            LOG_DEBUG_LOC(("表结构完整: " + table_name).c_str());
            return true;
        }
        
        LOG_INFO_LOC(("表 " + table_name + " 缺失字段: " + std::to_string(missing_fields.size())).c_str());
        
        // 添加缺失的字段
        for (const auto& field_name : missing_fields) {
            auto it = expected_fields.find(field_name);
            if (it == expected_fields.end()) continue;
            
            const FieldInfo& info = it->second;
            std::string alter_sql = "ALTER TABLE " + escapeSqlIdentifier(table_name) + 
                                   " ADD COLUMN " + escapeSqlIdentifier(field_name) + " " +
                                   fieldTypeToSql(info.type, info.length);
            
            try {
                main_db_->exec(alter_sql);
                LOG_INFO_LOC(("成功添加字段: " + field_name).c_str());
            } catch (const std::exception& e) {
                std::string error_str = e.what();
                if (error_str.find("duplicate column name") != std::string::npos) {
                    LOG_DEBUG_LOC(("字段已存在，跳过: " + field_name).c_str());
                } else {
                    LOG_ERROR_LOC(("添加字段失败: " + field_name + ", 错误: " + error_str).c_str());
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = "检查表结构失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::insertDeviceData(const std::string& table_name,
                              const std::map<std::string, DeviceDataItem>& data,
                              bool online_status) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        // 构建INSERT语句
        std::stringstream sql;
        std::string escaped_table = escapeSqlIdentifier(table_name);
        sql << "INSERT INTO " << escaped_table << " (timestamp, online_status";
        
        std::vector<std::string> field_names;
        for (const auto& item : data) {
            field_names.push_back(item.first);
            sql << ", " << escapeSqlIdentifier(item.first);
        }
        sql << ") VALUES (CURRENT_TIMESTAMP, " << (online_status ? 1 : 0);
        
        for (size_t i = 0; i < field_names.size(); ++i) {
            sql << ", ?";
        }
        sql << ");";
        
        SQLite::Statement query(*main_db_, sql.str());
        
        // 绑定参数
        int param_index = 1;
        for (const auto& field_name : field_names) {
            auto it = data.find(field_name);
            if (it != data.end()) {
                if (!bindValue(query, param_index++, it->second)) {
                    return false;
                }
            }
        }
        
        query.exec();
        return true;
    } catch (const std::exception& e) {
        last_error_ = "插入设备数据失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::batchInsertDeviceData(const std::string& table_name,
                                   const std::vector<std::map<std::string, DeviceDataItem>>& records,
                                   bool online_status) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        // 开始事务
        SQLite::Transaction transaction(*main_db_);
        
        int success_count = 0;
        for (const auto& record : records) {
            if (insertDeviceData(table_name, record, online_status)) {
                success_count++;
            }
        }
        
        // 提交事务
        transaction.commit();
        
        LOG_INFO_LOC(("批量插入设备数据完成: " + std::to_string(success_count) + "/" + 
                      std::to_string(records.size())).c_str());
        return success_count > 0;
    } catch (const std::exception& e) {
        last_error_ = "批量插入设备数据失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

std::map<std::string, DeviceDataItem> SqlCpp::queryLatestDeviceData(const std::string& table_name) {
    std::map<std::string, DeviceDataItem> result;
    
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        // 先获取所有字段名
        std::string pragma_sql = "PRAGMA table_info(" + escapeSqlIdentifier(table_name) + ");";
        std::vector<std::string> columns;
        
        SQLite::Statement pragma_query(*main_db_, pragma_sql);
        while (pragma_query.executeStep()) {
            std::string col_name = pragma_query.getColumn(1).getText();
            if (col_name != "id" && col_name != "timestamp" && col_name != "online_status") {
                columns.push_back(col_name);
            }
        }
        
        if (columns.empty()) {
            return result;
        }
        
        // 查询最新一条记录
        std::stringstream select_sql;
        select_sql << "SELECT ";
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) select_sql << ", ";
            select_sql << escapeSqlIdentifier(columns[i]);
        }
        select_sql << " FROM " << escapeSqlIdentifier(table_name) 
                   << " ORDER BY timestamp DESC LIMIT 1;";
        
        SQLite::Statement query(*main_db_, select_sql.str());
        
        if (query.executeStep()) {
            for (size_t i = 0; i < columns.size(); ++i) {
                DeviceDataItem item;
                item.field_name = columns[i];
                
                SQLite::Column col = query.getColumn(i);
                int col_type = col.getType();
                
                if (col_type == SQLite::INTEGER) {
                    item.type = FieldType::INTEGER;
                    item.value.int_val = col.getInt64();
                } else if (col_type == SQLite::FLOAT) {
                    item.type = FieldType::FLOAT;
                    item.value.float_val = col.getDouble();
                } else if (col_type == SQLite::TEXT) {
                    item.type = FieldType::TEXT;
                } else {
                    item.type = FieldType::INTEGER;
                    item.value.int_val = 0;
                }
                
                result[columns[i]] = item;
            }
        }
    } catch (const std::exception& e) {
        last_error_ = "查询最新设备数据失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
    }
    
    return result;
}

std::vector<std::map<std::string, DeviceDataItem>> SqlCpp::queryDeviceHistory(
    const std::string& table_name,
    const std::string& start_time,
    const std::string& end_time,
    int limit) {
    
    std::vector<std::map<std::string, DeviceDataItem>> result;
    
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        // 获取字段列表
        std::string pragma_sql = "PRAGMA table_info(" + escapeSqlIdentifier(table_name) + ");";
        std::vector<std::string> columns;
        
        SQLite::Statement pragma_query(*main_db_, pragma_sql);
        while (pragma_query.executeStep()) {
            std::string col_name = pragma_query.getColumn(1).getText();
            if (col_name != "id") {
                columns.push_back(col_name);
            }
        }
        
        if (columns.empty()) {
            return result;
        }
        
        // 构建查询语句
        std::stringstream select_sql;
        select_sql << "SELECT ";
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) select_sql << ", ";
            select_sql << escapeSqlIdentifier(columns[i]);
        }
        select_sql << " FROM " << escapeSqlIdentifier(table_name)
                   << " WHERE timestamp >= ? AND timestamp <= ?"
                   << " ORDER BY timestamp DESC LIMIT ?;";
        
        SQLite::Statement query(*main_db_, select_sql.str());
        query.bind(1, start_time);
        query.bind(2, end_time);
        query.bind(3, limit);
        
        while (query.executeStep()) {
            std::map<std::string, DeviceDataItem> record;
            
            for (size_t i = 0; i < columns.size(); ++i) {
                DeviceDataItem item;
                item.field_name = columns[i];
                
                SQLite::Column col = query.getColumn(i);
                int col_type = col.getType();
                
                if (col_type == SQLite::INTEGER) {
                    item.type = FieldType::INTEGER;
                    item.value.int_val = col.getInt64();
                } else if (col_type == SQLite::FLOAT) {
                    item.type = FieldType::FLOAT;
                    item.value.float_val = col.getDouble();
                } else {
                    item.type = FieldType::INTEGER;
                    item.value.int_val = 0;
                }
                
                record[columns[i]] = item;
            }
            
            result.push_back(record);
        }
        
        LOG_DEBUG_LOC(("查询到 " + std::to_string(result.size()) + " 条历史记录").c_str());
    } catch (const std::exception& e) {
        last_error_ = "查询设备历史数据失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
    }
    
    return result;
}

// ==================== 通用查询 ====================

bool SqlCpp::executeQuery(const std::string& sql,
                          std::function<int(int column_count, char** values, char** columns)> callback) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        SQLite::Statement query(*main_db_, sql);
        
        while (query.executeStep()) {
            int column_count = query.getColumnCount();
            
            // 转换列名为char**
            std::vector<const char*> column_names(column_count);
            std::vector<std::string> column_name_strings(column_count);
            for (int i = 0; i < column_count; ++i) {
                column_name_strings[i] = query.getColumnName(i);
                column_names[i] = column_name_strings[i].c_str();
            }
            
            // 转换值为char**
            std::vector<const char*> values(column_count);
            std::vector<std::string> value_strings(column_count);
            for (int i = 0; i < column_count; ++i) {
                SQLite::Column col = query.getColumn(i);
                if (col.isNull()) {
                    value_strings[i] = "";
                } else {
                    value_strings[i] = col.getText();
                }
                values[i] = value_strings[i].c_str();
            }
            
            int rc = callback(column_count, const_cast<char**>(values.data()), 
                            const_cast<char**>(column_names.data()));
            if (rc != 0) {
                break;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = "执行查询失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

// ==================== 辅助方法 ====================

std::string SqlCpp::getLastError() const {
    return last_error_;
}

std::string SqlCpp::escapeSqlIdentifier(const std::string& identifier) {
    // 使用双引号转义SQL标识符
    std::string escaped = identifier;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, "\"");
        pos += 2;
    }
    return "\"" + escaped + "\"";
}

std::string SqlCpp::fieldTypeToSql(FieldType type, int length) {
    switch (type) {
        case FieldType::INTEGER:
            return "INTEGER";
        case FieldType::FLOAT:
            return "REAL";
        case FieldType::TEXT:
            return "VARCHAR(" + std::to_string(length) + ")";
        case FieldType::BOOLEAN:
            return "BOOLEAN";
        case FieldType::DATETIME:
            return "DATETIME";
        default:
            return "TEXT";
    }
}

bool SqlCpp::bindValue(SQLite::Statement& stmt, int index, const DeviceDataItem& item) {
    try {
        switch (item.type) {
            case FieldType::INTEGER:
                stmt.bind(index, item.value.int_val);
                break;
            case FieldType::FLOAT:
                stmt.bind(index, item.value.float_val);
                break;
            case FieldType::BOOLEAN:
                stmt.bind(index, item.value.bool_val ? 1 : 0);
                break;
            case FieldType::TEXT:
                // 文本类型需要在构造DeviceDataItem时特殊处理
                stmt.bind(index, "");
                break;
            default:
                stmt.bind(index);
                break;
        }
        return true;
    } catch (const std::exception& e) {
        last_error_ = "绑定参数失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

std::string SqlCpp::getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm_buf;
    localtime_r(&time_t_now, &tm_buf);
    
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
    
    std::stringstream ss;
    ss << buffer << "." << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

// ==================== RegisterData 相关方法实现 ====================

bool SqlCpp::insertDeviceDataFromRegister(const std::string& table_name,
                                          const std::unordered_map<std::string, RegisterData>& register_data,
                                          bool online_status) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    // 首先确保表结构存在
    if (!createOrUpdateDeviceTableFromRegister(table_name, register_data)) {
        LOG_ERROR_LOC(("创建或更新表结构失败: " + table_name).c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    // 调用内部实现（不重复获取锁）
    return insertDeviceDataFromRegisterInternal(table_name, register_data, online_status);
}

// 内部实现，假设调用者已经持有锁
bool SqlCpp::insertDeviceDataFromRegisterInternal(const std::string& table_name,
                                                  const std::unordered_map<std::string, RegisterData>& register_data,
                                                  bool online_status) {
    try {
        // 构建INSERT语句
        std::stringstream sql;
        std::string escaped_table = escapeSqlIdentifier(table_name);
        sql << "INSERT INTO " << escaped_table << " (timestamp, online_status";
        
        std::vector<std::string> field_names;
        for (const auto& item : register_data) {
            field_names.push_back(item.first);
            sql << ", " << escapeSqlIdentifier(item.first);
        }
        sql << ") VALUES (datetime('now', 'localtime'), " << (online_status ? 1 : 0);
        
        for (size_t i = 0; i < field_names.size(); ++i) {
            sql << ", ?";
        }
        sql << ");";
        
        SQLite::Statement query(*main_db_, sql.str());
        
        // 绑定参数
        int param_index = 1;
        for (const auto& field_name : field_names) {
            auto it = register_data.find(field_name);
            if (it != register_data.end()) {
                // RegisterData中的value是double类型，直接绑定
                query.bind(param_index++, it->second.value);
            }
        }
        
        query.exec();
        return true;
    } catch (const std::exception& e) {
        last_error_ = "插入设备数据失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::batchInsertDeviceDataFromRegister(
    const std::string& table_name,
    const std::vector<std::unordered_map<std::string, RegisterData>>& records,
    bool online_status) {
    
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    if (records.empty()) {
        LOG_WARNING_LOC("批量插入数据为空");
        return false;
    }
    
    // 首先确保表结构存在（使用第一条记录）
    if (!createOrUpdateDeviceTableFromRegister(table_name, records[0])) {
        LOG_ERROR_LOC(("创建或更新表结构失败: " + table_name).c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    try {
        // 开始事务
        SQLite::Transaction transaction(*main_db_);
        
        int success_count = 0;
        for (const auto& record : records) {
            // 调用内部版本，避免重复获取锁导致死锁
            if (insertDeviceDataFromRegisterInternal(table_name, record, online_status)) {
                success_count++;
            }
        }
        
        // 提交事务
        transaction.commit();
        
        LOG_INFO_LOC(("批量插入设备数据完成: " + std::to_string(success_count) + "/" + 
                      std::to_string(records.size())).c_str());
        return success_count > 0;
    } catch (const std::exception& e) {
        last_error_ = "批量插入设备数据失败: " + std::string(e.what());
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
}

bool SqlCpp::createOrUpdateDeviceTableFromRegister(
    const std::string& table_name,
    const std::unordered_map<std::string, RegisterData>& register_data) {
    
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    // 将RegisterData转换为FieldInfo
    std::map<std::string, FieldInfo> fields;
    for (const auto& pair : register_data) {
        const std::string& name = pair.first;
        const RegisterData& reg = pair.second;
        
        // 根据datatype确定字段类型
        FieldType field_type = FieldType::FLOAT;  // 默认使用FLOAT，因为RegisterData的value是double
        
        // 如果datatype包含INT，可以考虑使用INTEGER
        if (reg.datatype.find("INT") != std::string::npos && 
            reg.datatype.find("UINT") == std::string::npos) {
            // 检查是否有小数部分，如果没有可以存为INTEGER
            if (reg.value == static_cast<int64_t>(reg.value)) {
                field_type = FieldType::INTEGER;
            }
        }
        
        fields[name] = FieldInfo(name, field_type, 50, true, false);
    }
    
    // 先尝试创建表（如果不存在）
    if (!createDeviceTable(table_name, fields)) {
        LOG_ERROR_LOC(("创建设备表失败: " + table_name).c_str());
        return false;
    }
    
    // 检查并更新表结构
    return checkAndUpdateTableStructure(table_name, fields);
}
