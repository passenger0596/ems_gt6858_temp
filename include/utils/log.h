#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <memory>

// 日志级别枚举
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    CRITICAL = 5
};

// 日志颜色控制（ANSI转义序列）
namespace LogColor {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* BLACK = "\033[30m";
    constexpr const char* RED = "\033[31m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* BLUE = "\033[34m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* CYAN = "\033[36m";
    constexpr const char* WHITE = "\033[37m";
    constexpr const char* BOLD = "\033[1m";
}

class Logger {
public:
    // 获取全局单例实例
    static Logger& getInstance();
    
    // 设置日志级别
    void setLevel(LogLevel level);
    
    // 设置是否启用彩色输出
    void setColorEnabled(bool enabled);
    
    // 设置是否显示文件位置信息（文件名:行号）
    void setLocationEnabled(bool enabled);
    
    // 日志输出方法
    void trace(const std::string& message);
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
    // 带文件位置信息的日志输出方法
    void trace(const std::string& message, const char* file, int line, const char* function);
    void debug(const std::string& message, const char* file, int line, const char* function);
    void info(const std::string& message, const char* file, int line, const char* function);
    void warning(const std::string& message, const char* file, int line, const char* function);
    void error(const std::string& message, const char* file, int line, const char* function);
    void critical(const std::string& message, const char* file, int line, const char* function);

    // 格式化输出（类似printf风格）
    template<typename... Args>
    void trace(const char* format, Args... args);
    
    template<typename... Args>
    void debug(const char* format, Args... args);
    
    template<typename... Args>
    void info(const char* format, Args... args);
    
    template<typename... Args>
    void warning(const char* format, Args... args);
    
    template<typename... Args>
    void error(const char* format, Args... args);
    
    template<typename... Args>
    void critical(const char* format, Args... args);

private:
    Logger();
    ~Logger() = default;
    
    // 禁止拷贝和赋值
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    // 内部日志输出方法
    void log(LogLevel level, const std::string& message);
    
    // 带文件位置信息的内部日志输出方法
    void logWithLocation(LogLevel level, const std::string& message, 
                        const char* file, int line, const char* function);
    
    // 从完整路径中提取文件名
    std::string extractFilename(const char* filepath);

    // 获取当前时间字符串
    std::string getCurrentTime();
    
    // 获取日志级别字符串
    std::string getLevelString(LogLevel level);
    
    // 获取日志级别对应的颜色
    const char* getLevelColor(LogLevel level);
    
    // 格式化字符串（类似sprintf）
    template<typename... Args>
    std::string formatString(const char* format, Args... args);
    
    LogLevel current_level_;
    bool color_enabled_;
    bool location_enabled_;  // 是否显示文件位置信息
    std::mutex log_mutex_;
};

// 全局单例实例
#define LOGGER Logger::getInstance()

// 便捷宏定义（类似loguru的使用方式）
// 基础宏 - 不包含文件位置信息
#define LOG_TRACE(message) LOGGER.trace(message)
#define LOG_DEBUG(message) LOGGER.debug(message)
#define LOG_INFO(message) LOGGER.info(message)
#define LOG_WARNING(message) LOGGER.warning(message)
#define LOG_ERROR(message) LOGGER.error(message)
#define LOG_CRITICAL(message) LOGGER.critical(message)

// 格式化输出宏 - 不包含文件位置信息
#define LOG_TRACE_F(format, ...) LOGGER.trace(format, __VA_ARGS__)
#define LOG_DEBUG_F(format, ...) LOGGER.debug(format, __VA_ARGS__)
#define LOG_INFO_F(format, ...) LOGGER.info(format, __VA_ARGS__)
#define LOG_WARNING_F(format, ...) LOGGER.warning(format, __VA_ARGS__)
#define LOG_ERROR_F(format, ...) LOGGER.error(format, __VA_ARGS__)
#define LOG_CRITICAL_F(format, ...) LOGGER.critical(format, __VA_ARGS__)

// ==================== 带文件位置信息的宏（类似 loguru）====================
// 这些宏会自动记录文件名、行号和函数名
#define LOG_TRACE_LOC(message) LOGGER.trace(std::string(message), __FILE__, __LINE__, __FUNCTION__)
#define LOG_DEBUG_LOC(message) LOGGER.debug(std::string(message), __FILE__, __LINE__, __FUNCTION__)
#define LOG_INFO_LOC(message) LOGGER.info(std::string(message), __FILE__, __LINE__, __FUNCTION__)
#define LOG_WARNING_LOC(message) LOGGER.warning(std::string(message), __FILE__, __LINE__, __FUNCTION__)
#define LOG_ERROR_LOC(message) LOGGER.error(std::string(message), __FILE__, __LINE__, __FUNCTION__)
#define LOG_CRITICAL_LOC(message) LOGGER.critical(std::string(message), __FILE__, __LINE__, __FUNCTION__)

// 带文件位置的格式化输出宏（已移除，建议使用字符串拼接）
// 原因：formatString 是私有方法，无法在宏中访问
// 替代方案：使用 LOG_INFO_LOC("消息" + std::to_string(value))

// ==================== 模板实现（必须放在头文件中） ====================

template<typename... Args>
std::string Logger::formatString(const char* format, Args... args) {
    if constexpr (sizeof...(Args) == 0) {
        return std::string(format);
    } else {
        int size_s = std::snprintf(nullptr, 0, format, args...) + 1;
        if (size_s <= 0) {
            return "Format error";
        }
        auto size = static_cast<size_t>(size_s);
        std::unique_ptr<char[]> buf(new char[size]);
        std::snprintf(buf.get(), size, format, args...);
        return std::string(buf.get(), buf.get() + size - 1);
    }
}

template<typename... Args>
void Logger::trace(const char* format, Args... args) {
    if (LogLevel::TRACE >= current_level_) {
        trace(formatString(format, args...));
    }
}

template<typename... Args>
void Logger::debug(const char* format, Args... args) {
    if (LogLevel::DEBUG >= current_level_) {
        debug(formatString(format, args...));
    }
}

template<typename... Args>
void Logger::info(const char* format, Args... args) {
    if (LogLevel::INFO >= current_level_) {
        info(formatString(format, args...));
    }
}

template<typename... Args>
void Logger::warning(const char* format, Args... args) {
    if (LogLevel::WARNING >= current_level_) {
        warning(formatString(format, args...));
    }
}

template<typename... Args>
void Logger::error(const char* format, Args... args) {
    if (LogLevel::ERROR >= current_level_) {
        error(formatString(format, args...));
    }
}

template<typename... Args>
void Logger::critical(const char* format, Args... args) {
    if (LogLevel::CRITICAL >= current_level_) {
        critical(formatString(format, args...));
    }
}



#endif // LOG_H
