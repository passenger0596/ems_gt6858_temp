#include "sqldatabase.h"
#include "device.h"  // 包含RegisterData定义
#include "log.h"
#include <sstream>
#include <algorithm>
#include <ctime>
#include <cstring>

// ==================== 单例实现 ====================

SqlDatabase& SqlDatabase::getInstance() {
    static SqlDatabase instance;
    return instance;
}

SqlDatabase::SqlDatabase() 
    : main_db_(nullptr), log_db_(nullptr), alarm_db_(nullptr), 
      initialized_(false) {
}

SqlDatabase::~SqlDatabase() {
    close();
}

// ==================== 初始化与关闭 ====================

bool SqlDatabase::initialize(const std::string& main_db_path,
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
    
    const char* create_log_table_sql = 
        "CREATE TABLE IF NOT EXISTS operation ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "type TEXT NOT NULL,"
        "source TEXT NOT NULL,"
        "desc TEXT NOT NULL"
        ");";
    
    char* err_msg = nullptr;
    if (sqlite3_exec(log_db_, create_log_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        last_error_ = "创建操作日志表失败: " + std::string(err_msg ? err_msg : "未知错误");
        LOG_ERROR_LOC(last_error_.c_str());
        sqlite3_free(err_msg);
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
    
    const char* create_alarm_table_sql = 
        "CREATE TABLE IF NOT EXISTS alarmHistory ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "等级 TEXT,"
        "告警时间 TEXT DEFAULT (datetime('now', 'localtime')),"
        "设备名称 TEXT,"
        "告警描述 TEXT,"
        "恢复时间 TEXT DEFAULT 'NA'"
        ");";
    
    if (sqlite3_exec(alarm_db_, create_alarm_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        last_error_ = "创建告警历史表失败: " + std::string(err_msg ? err_msg : "未知错误");
        LOG_ERROR_LOC(last_error_.c_str());
        sqlite3_free(err_msg);
        close();
        return false;
    }
    
    initialized_ = true;
    LOG_INFO_LOC("数据库初始化成功");
    return true;
}

void SqlDatabase::close() {
    std::lock_guard<std::mutex> lock_main(main_mutex_);
    std::lock_guard<std::mutex> lock_log(log_mutex_);
    std::lock_guard<std::mutex> lock_alarm(alarm_mutex_);
    
    closeDatabase(main_db_);
    closeDatabase(log_db_);
    closeDatabase(alarm_db_);
    
    initialized_ = false;
    LOG_INFO_LOC("数据库连接已关闭");
}

bool SqlDatabase::openDatabase(sqlite3*& db, const std::string& path) {
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        last_error_ = "无法打开数据库: " + path + ", 错误: " + sqlite3_errmsg(db);
        LOG_ERROR_LOC(last_error_.c_str());
        sqlite3_close(db);
        db = nullptr;
        return false;
    }
    
    // 启用WAL模式以提高并发性能
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    
    // 设置busy timeout为30秒
    sqlite3_busy_timeout(db, 30000);
    
    return true;
}

void SqlDatabase::closeDatabase(sqlite3*& db) {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

// ==================== 操作日志相关 ====================

bool SqlDatabase::insertOperationLog(const OperationLog& log) {
    if (!initialized_ || !log_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    const char* sql = 
        "INSERT INTO operation (timestamp, type, source, desc) VALUES (?, ?, ?, ?);";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(log_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(log_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, log.timestamp.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, log.type.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, log.source.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, log.desc.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        last_error_ = "插入操作日志失败: " + std::string(sqlite3_errmsg(log_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    LOG_DEBUG_LOC(("操作日志插入成功: " + log.desc).c_str());
    return true;
}

bool SqlDatabase::batchInsertOperationLogs(const std::vector<OperationLog>& logs) {
    if (!initialized_ || !log_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    // 开始事务
    if (sqlite3_exec(log_db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        last_error_ = "开始事务失败: " + std::string(sqlite3_errmsg(log_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    const char* sql = 
        "INSERT INTO operation (timestamp, type, source, desc) VALUES (?, ?, ?, ?);";
    
    int success_count = 0;
    for (const auto& log : logs) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(log_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_WARNING_LOC(("准备SQL语句失败: " + std::string(sqlite3_errmsg(log_db_))).c_str());
            continue;
        }
        
        sqlite3_bind_text(stmt, 1, log.timestamp.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, log.type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, log.source.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, log.desc.c_str(), -1, SQLITE_STATIC);
        
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        
        if (rc == SQLITE_DONE) {
            success_count++;
        } else {
            LOG_WARNING_LOC("插入单条日志失败");
        }
    }
    
    // 提交事务
    if (sqlite3_exec(log_db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(log_db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        last_error_ = "提交事务失败: " + std::string(sqlite3_errmsg(log_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    LOG_INFO_LOC(("批量插入操作日志完成: " + std::to_string(success_count) + "/" + 
                  std::to_string(logs.size())).c_str());
    return success_count > 0;
}

// ==================== 告警历史相关 ====================

bool SqlDatabase::insertAlarmHistory(const AlarmHistory& alarm) {
    if (!initialized_ || !alarm_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(alarm_mutex_);
    
    const char* sql = 
        "INSERT INTO alarmHistory (等级, 告警时间, 设备名称, 告警描述, 恢复时间) "
        "VALUES (?, ?, ?, ?, ?);";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(alarm_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(alarm_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, alarm.level.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, alarm.alarm_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, alarm.device_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, alarm.description.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, alarm.recovery_time.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        last_error_ = "插入告警记录失败: " + std::string(sqlite3_errmsg(alarm_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    LOG_INFO_LOC(("告警记录插入成功: " + alarm.level + ":" + alarm.device_name + ":" + alarm.description).c_str());
    return true;
}

bool SqlDatabase::updateAlarmRecoveryTime(const std::string& alarm_time,
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
    
    const char* sql = 
        "UPDATE alarmHistory SET 恢复时间 = ? "
        "WHERE 告警时间 = ? AND 设备名称 = ? AND 告警描述 = ?;";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(alarm_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(alarm_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, final_recovery_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, alarm_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, device_name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, description.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        last_error_ = "更新告警恢复时间失败: " + std::string(sqlite3_errmsg(alarm_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    LOG_INFO_LOC(("成功更新告警恢复时间: " + device_name + ":" + description).c_str());
    return true;
}

std::vector<AlarmHistory> SqlDatabase::queryUnrecoveredAlarms() {
    std::vector<AlarmHistory> result;
    
    if (!initialized_ || !alarm_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    std::lock_guard<std::mutex> lock(alarm_mutex_);
    
    const char* sql = 
        "SELECT id, 等级, 告警时间, 设备名称, 告警描述, 恢复时间 "
        "FROM alarmHistory WHERE 恢复时间 = 'NA' OR 恢复时间 IS NULL;";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(alarm_db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(alarm_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AlarmHistory alarm;
        alarm.id = sqlite3_column_int(stmt, 0);
        alarm.level = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        alarm.alarm_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        alarm.device_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        alarm.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        alarm.recovery_time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        
        result.push_back(alarm);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG_LOC(("查询到 " + std::to_string(result.size()) + " 条未恢复告警").c_str());
    return result;
}

// ==================== 设备数据相关 ====================

bool SqlDatabase::createDeviceTable(const std::string& table_name,
                                   const std::map<std::string, FieldInfo>& fields) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
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
    
    char* err_msg = nullptr;
    if (sqlite3_exec(main_db_, sql.str().c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        last_error_ = "创建设备表失败: " + std::string(err_msg ? err_msg : "未知错误");
        LOG_ERROR_LOC(last_error_.c_str());
        sqlite3_free(err_msg);
        return false;
    }
    
    LOG_INFO_LOC(("设备表创建成功: " + table_name).c_str());
    return true;
}

bool SqlDatabase::checkAndUpdateTableStructure(const std::string& table_name,
                                              const std::map<std::string, FieldInfo>& expected_fields) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    // 检查表是否存在
    const char* check_table_sql = "SELECT count(*) FROM sqlite_master WHERE type='table' AND name=?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(main_db_, check_table_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_STATIC);
    
    bool table_exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        table_exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    
    if (!table_exists) {
        last_error_ = "表不存在: " + table_name;
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    // 获取现有字段
    std::string pragma_sql = "PRAGMA table_info(" + escapeSqlIdentifier(table_name) + ");";
    std::map<std::string, std::string> existing_columns;
    
    if (sqlite3_prepare_v2(main_db_, pragma_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备PRAGMA语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (col_name) {
            existing_columns[col_name] = "";
        }
    }
    sqlite3_finalize(stmt);
    
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
        
        char* err_msg = nullptr;
        if (sqlite3_exec(main_db_, alter_sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
            // 检查是否是重复字段错误
            std::string error_str = err_msg ? err_msg : "未知错误";
            if (error_str.find("duplicate column name") != std::string::npos) {
                LOG_DEBUG_LOC(("字段已存在，跳过: " + field_name).c_str());
            } else {
                LOG_ERROR_LOC(("添加字段失败: " + field_name + ", 错误: " + error_str).c_str());
            }
            sqlite3_free(err_msg);
        } else {
            LOG_INFO_LOC(("成功添加字段: " + field_name).c_str());
        }
    }
    
    return true;
}

bool SqlDatabase::insertDeviceData(const std::string& table_name,
                                  const std::map<std::string, DeviceDataItem>& data,
                                  bool online_status) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
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
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(main_db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    // 绑定参数
    int param_index = 1;
    for (const auto& field_name : field_names) {
        auto it = data.find(field_name);
        if (it != data.end()) {
            if (!bindValue(stmt, param_index++, it->second)) {
                sqlite3_finalize(stmt);
                return false;
            }
        }
    }
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        last_error_ = "插入设备数据失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    return true;
}

bool SqlDatabase::batchInsertDeviceData(const std::string& table_name,
                                       const std::vector<std::map<std::string, DeviceDataItem>>& records,
                                       bool online_status) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    // 开始事务
    if (sqlite3_exec(main_db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        last_error_ = "开始事务失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    int success_count = 0;
    for (const auto& record : records) {
        if (insertDeviceData(table_name, record, online_status)) {
            success_count++;
        }
    }
    
    // 提交事务
    if (sqlite3_exec(main_db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(main_db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        last_error_ = "提交事务失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    LOG_INFO_LOC(("批量插入设备数据完成: " + std::to_string(success_count) + "/" + 
                  std::to_string(records.size())).c_str());
    return success_count > 0;
}

std::map<std::string, DeviceDataItem> SqlDatabase::queryLatestDeviceData(const std::string& table_name) {
    std::map<std::string, DeviceDataItem> result;
    
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    // 先获取所有字段名
    std::string pragma_sql = "PRAGMA table_info(" + escapeSqlIdentifier(table_name) + ");";
    std::vector<std::string> columns;
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(main_db_, pragma_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备PRAGMA语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (col_name && std::string(col_name) != "id" && 
            std::string(col_name) != "timestamp" && 
            std::string(col_name) != "online_status") {
            columns.push_back(col_name);
        }
    }
    sqlite3_finalize(stmt);
    
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
    
    if (sqlite3_prepare_v2(main_db_, select_sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备查询语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        for (size_t i = 0; i < columns.size(); ++i) {
            DeviceDataItem item;
            item.field_name = columns[i];
            
            int col_type = sqlite3_column_type(stmt, i);
            switch (col_type) {
                case SQLITE_INTEGER:
                    item.type = FieldType::INTEGER;
                    item.value.int_val = sqlite3_column_int64(stmt, i);
                    break;
                case SQLITE_FLOAT:
                    item.type = FieldType::FLOAT;
                    item.value.float_val = sqlite3_column_double(stmt, i);
                    break;
                case SQLITE_TEXT: {
                    item.type = FieldType::TEXT;
                    // 文本类型需要特殊处理
                    break;
                }
                default:
                    item.type = FieldType::INTEGER;
                    item.value.int_val = 0;
                    break;
            }
            
            result[columns[i]] = item;
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::map<std::string, DeviceDataItem>> SqlDatabase::queryDeviceHistory(
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
    
    // 获取字段列表
    std::string pragma_sql = "PRAGMA table_info(" + escapeSqlIdentifier(table_name) + ");";
    std::vector<std::string> columns;
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(main_db_, pragma_sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备PRAGMA语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* col_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (col_name && std::string(col_name) != "id") {
            columns.push_back(col_name);
        }
    }
    sqlite3_finalize(stmt);
    
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
    
    if (sqlite3_prepare_v2(main_db_, select_sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备查询语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return result;
    }
    
    sqlite3_bind_text(stmt, 1, start_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, end_time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, limit);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::map<std::string, DeviceDataItem> record;
        
        for (size_t i = 0; i < columns.size(); ++i) {
            DeviceDataItem item;
            item.field_name = columns[i];
            
            int col_type = sqlite3_column_type(stmt, i);
            switch (col_type) {
                case SQLITE_INTEGER:
                    item.type = FieldType::INTEGER;
                    item.value.int_val = sqlite3_column_int64(stmt, i);
                    break;
                case SQLITE_FLOAT:
                    item.type = FieldType::FLOAT;
                    item.value.float_val = sqlite3_column_double(stmt, i);
                    break;
                default:
                    item.type = FieldType::INTEGER;
                    item.value.int_val = 0;
                    break;
            }
            
            record[columns[i]] = item;
        }
        
        result.push_back(record);
    }
    
    sqlite3_finalize(stmt);
    LOG_DEBUG_LOC(("查询到 " + std::to_string(result.size()) + " 条历史记录").c_str());
    return result;
}

// ==================== 通用查询 ====================

bool SqlDatabase::executeQuery(const std::string& sql,
                              std::function<int(int column_count, char** values, char** columns)> callback) {
    if (!initialized_ || !main_db_) {
        last_error_ = "数据库未初始化";
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    std::lock_guard<std::mutex> lock(main_mutex_);
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(main_db_, sql.c_str(), 
                         [](void* cb_data, int cols, char** vals, char** names) -> int {
                             auto* callback_ptr = reinterpret_cast<decltype(callback)*>(cb_data);
                             return (*callback_ptr)(cols, vals, names);
                         },
                         &callback, &err_msg);
    
    if (rc != SQLITE_OK) {
        last_error_ = "执行查询失败: " + std::string(err_msg ? err_msg : "未知错误");
        LOG_ERROR_LOC(last_error_.c_str());
        sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

// ==================== 辅助方法 ====================

std::string SqlDatabase::getLastError() const {
    return last_error_;
}

std::string SqlDatabase::escapeSqlIdentifier(const std::string& identifier) {
    // 使用双引号转义SQL标识符
    std::string escaped = identifier;
    size_t pos = 0;
    while ((pos = escaped.find('"', pos)) != std::string::npos) {
        escaped.insert(pos, "\"");
        pos += 2;
    }
    return "\"" + escaped + "\"";
}

std::string SqlDatabase::fieldTypeToSql(FieldType type, int length) {
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

bool SqlDatabase::bindValue(sqlite3_stmt* stmt, int index, const DeviceDataItem& item) {
    int rc = SQLITE_OK;
    
    switch (item.type) {
        case FieldType::INTEGER:
            rc = sqlite3_bind_int64(stmt, index, item.value.int_val);
            break;
        case FieldType::FLOAT:
            rc = sqlite3_bind_double(stmt, index, item.value.float_val);
            break;
        case FieldType::BOOLEAN:
            rc = sqlite3_bind_int(stmt, index, item.value.bool_val ? 1 : 0);
            break;
        case FieldType::TEXT:
            // 文本类型需要在构造DeviceDataItem时特殊处理
            rc = sqlite3_bind_null(stmt, index);
            break;
        default:
            rc = sqlite3_bind_null(stmt, index);
            break;
    }
    
    if (rc != SQLITE_OK) {
        last_error_ = "绑定参数失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    return true;
}

std::string SqlDatabase::getCurrentTimeString() {
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

bool SqlDatabase::insertDeviceDataFromRegister(const std::string& table_name,
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
bool SqlDatabase::insertDeviceDataFromRegisterInternal(const std::string& table_name,
                                                       const std::unordered_map<std::string, RegisterData>& register_data,
                                                       bool online_status) {
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
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(main_db_, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        last_error_ = "准备SQL语句失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    // 绑定参数
    int param_index = 1;
    for (const auto& field_name : field_names) {
        auto it = register_data.find(field_name);
        if (it != register_data.end()) {
            // RegisterData中的value是double类型，直接绑定
            sqlite3_bind_double(stmt, param_index++, it->second.value);
        }
    }
    
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        last_error_ = "插入设备数据失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    return true;
}

bool SqlDatabase::batchInsertDeviceDataFromRegister(
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
    
    // 开始事务
    if (sqlite3_exec(main_db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        last_error_ = "开始事务失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    int success_count = 0;
    for (const auto& record : records) {
        // 调用内部版本，避免重复获取锁导致死锁
        if (insertDeviceDataFromRegisterInternal(table_name, record, online_status)) {
            success_count++;
        }
    }
    
    // 提交事务
    if (sqlite3_exec(main_db_, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_exec(main_db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        last_error_ = "提交事务失败: " + std::string(sqlite3_errmsg(main_db_));
        LOG_ERROR_LOC(last_error_.c_str());
        return false;
    }
    
    LOG_INFO_LOC(("批量插入设备数据完成: " + std::to_string(success_count) + "/" + 
                  std::to_string(records.size())).c_str());
    return success_count > 0;
}

bool SqlDatabase::createOrUpdateDeviceTableFromRegister(
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
