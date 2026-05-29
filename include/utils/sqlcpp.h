#ifndef SQLCPP_H
#define SQLCPP_H

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <functional>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

// 前向声明设备相关结构
struct RegisterData;

// 复用现有的数据结构定义（与sqldatabase.h保持一致）
// 字段类型枚举
enum class FieldType {
    INTEGER,
    FLOAT,
    TEXT,
    BOOLEAN,
    DATETIME
};

// 字段信息结构
struct FieldInfo {
    std::string name;
    FieldType type;
    int length;           // 仅用于TEXT类型
    bool nullable;
    bool unique;
    
    FieldInfo() : type(FieldType::INTEGER), length(10), nullable(true), unique(false) {}
    FieldInfo(const std::string& n, FieldType t, int l = 10, bool null = true, bool uniq = false)
        : name(n), type(t), length(l), nullable(null), unique(uniq) {}
};

// 数据值联合体
union DataValue {
    int64_t int_val;
    double float_val;
    bool bool_val;
    
    DataValue() : int_val(0) {}
};

// 设备数据项
struct DeviceDataItem {
    std::string field_name;
    DataValue value;
    FieldType type;
    
    DeviceDataItem() : type(FieldType::INTEGER) {}
    DeviceDataItem(const std::string& name, int64_t val) 
        : field_name(name), type(FieldType::INTEGER) { value.int_val = val; }
    DeviceDataItem(const std::string& name, double val) 
        : field_name(name), type(FieldType::FLOAT) { value.float_val = val; }
    DeviceDataItem(const std::string& name, bool val) 
        : field_name(name), type(FieldType::BOOLEAN) { value.bool_val = val; }
    DeviceDataItem(const std::string& name, const std::string& val) 
        : field_name(name), type(FieldType::TEXT) { 
            // 对于文本类型，需要特殊处理
        }
};

// 操作日志结构
struct OperationLog {
    std::string timestamp;
    std::string type;      // 操作类型
    std::string source;    // 来源
    std::string desc;      // 描述
    
    OperationLog() {}
    OperationLog(const std::string& ts, const std::string& t, 
                 const std::string& s, const std::string& d)
        : timestamp(ts), type(t), source(s), desc(d) {}
};

// 告警历史记录结构
struct AlarmHistory {
    int id;
    std::string level;         // 告警等级
    std::string alarm_time;    // 告警时间
    std::string device_name;   // 设备名称
    std::string description;   // 告警描述
    std::string recovery_time; // 恢复时间
    
    AlarmHistory() : id(0) {}
    AlarmHistory(const std::string& lvl, const std::string& time, 
                 const std::string& dev, const std::string& desc,
                 const std::string& rec = "NA")
        : id(0), level(lvl), alarm_time(time), device_name(dev), 
          description(desc), recovery_time(rec) {}
};

class SqlCpp {
public:
    // 获取单例实例
    static SqlCpp& getInstance();
    
    // 初始化数据库连接
    bool initialize(const std::string& main_db_path = "storage_system.db",
                   const std::string& log_db_path = "operation_log.db",
                   const std::string& alarm_db_path = "alarmHistory.db");
    
    // 关闭数据库连接
    void close();
    
    // ==================== 操作日志相关 ====================
    // 插入操作日志
    bool insertOperationLog(const OperationLog& log);
    
    // 批量插入操作日志
    bool batchInsertOperationLogs(const std::vector<OperationLog>& logs);
    
    // ==================== 告警历史相关 ====================
    // 插入告警记录
    bool insertAlarmHistory(const AlarmHistory& alarm);
    
    // 更新告警恢复时间
    bool updateAlarmRecoveryTime(const std::string& alarm_time, 
                                 const std::string& device_name,
                                 const std::string& description,
                                 const std::string& recovery_time = "");
    
    // 查询未恢复的告警
    std::vector<AlarmHistory> queryUnrecoveredAlarms();
    
    // ==================== 设备数据相关 ====================
    // 动态创建设备表（如果不存在）
    bool createDeviceTable(const std::string& table_name, 
                          const std::map<std::string, FieldInfo>& fields);
    
    // 检查并更新表结构（添加新字段）
    bool checkAndUpdateTableStructure(const std::string& table_name,
                                     const std::map<std::string, FieldInfo>& expected_fields);
    
    // 插入设备数据（使用DeviceDataItem格式）
    bool insertDeviceData(const std::string& table_name,
                         const std::map<std::string, DeviceDataItem>& data,
                         bool online_status = true);
    
    // 批量插入设备数据（使用DeviceDataItem格式）
    bool batchInsertDeviceData(const std::string& table_name,
                              const std::vector<std::map<std::string, DeviceDataItem>>& records,
                              bool online_status = true);
    
    // 【新增】插入设备数据（直接使用RegisterData格式，更方便）
    bool insertDeviceDataFromRegister(const std::string& table_name,
                                     const std::unordered_map<std::string, RegisterData>& register_data,
                                     bool online_status = true);
    
    // 【新增】批量插入设备数据（直接使用RegisterData格式）
    bool batchInsertDeviceDataFromRegister(const std::string& table_name,
                                          const std::vector<std::unordered_map<std::string, RegisterData>>& records,
                                          bool online_status = true);
    
    // 【新增】根据RegisterData自动创建或更新表结构
    bool createOrUpdateDeviceTableFromRegister(const std::string& table_name,
                                              const std::unordered_map<std::string, RegisterData>& register_data);
    
    // 查询设备最新数据
    std::map<std::string, DeviceDataItem> queryLatestDeviceData(const std::string& table_name);
    
    // 查询设备历史数据（按时间范围）
    std::vector<std::map<std::string, DeviceDataItem>> queryDeviceHistory(
        const std::string& table_name,
        const std::string& start_time,
        const std::string& end_time,
        int limit = 100);
    
    // ==================== 通用查询 ====================
    // 执行自定义SQL查询（只读）
    bool executeQuery(const std::string& sql, 
                     std::function<int(int column_count, char** values, char** columns)> callback);
    
    // 获取最后错误信息
    std::string getLastError() const;
    
    // 检查数据库是否已初始化
    bool isInitialized() const { return initialized_; }

private:
    SqlCpp();
    ~SqlCpp();
    
    // 禁止拷贝和赋值
    SqlCpp(const SqlCpp&) = delete;
    SqlCpp& operator=(const SqlCpp&) = delete;
    
    // 内部辅助方法
    bool openDatabase(std::unique_ptr<SQLite::Database>& db, const std::string& path);
    void closeDatabase(std::unique_ptr<SQLite::Database>& db);
    
    // 转义SQL标识符
    std::string escapeSqlIdentifier(const std::string& identifier);
    
    // 将FieldType转换为SQL类型字符串
    std::string fieldTypeToSql(FieldType type, int length = 10);
    
    // 绑定参数到prepared statement
    bool bindValue(SQLite::Statement& stmt, int index, const DeviceDataItem& item);
    
    // 获取当前时间字符串
    std::string getCurrentTimeString();
    
    // 【新增】内部实现，假设调用者已经持有锁（避免死锁）
    bool insertDeviceDataFromRegisterInternal(const std::string& table_name,
                                             const std::unordered_map<std::string, RegisterData>& register_data,
                                             bool online_status);

    // 主数据库（设备数据）
    std::unique_ptr<SQLite::Database> main_db_;
    
    // 操作日志数据库
    std::unique_ptr<SQLite::Database> log_db_;
    
    // 告警历史数据库
    std::unique_ptr<SQLite::Database> alarm_db_;
    
    // 互斥锁保护并发访问
    std::mutex main_mutex_;
    std::mutex log_mutex_;
    std::mutex alarm_mutex_;
    
    // 初始化标志
    bool initialized_;
    
    // 最后错误信息
    std::string last_error_;
};

// 便捷宏定义
#define SQL_CPP SqlCpp::getInstance()

#endif // SQLCPP_H
