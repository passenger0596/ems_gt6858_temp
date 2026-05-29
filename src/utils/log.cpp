#include "log.h"
#include <cstdarg>
#include <cstring>

// 全局单例实例
Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() 
    : current_level_(LogLevel::INFO), color_enabled_(true), location_enabled_(true) {
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
}

void Logger::setColorEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    color_enabled_ = enabled;
}

void Logger::setLocationEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    location_enabled_ = enabled;
}

void Logger::trace(const std::string& message) {
    log(LogLevel::TRACE, message);
}

void Logger::debug(const std::string& message) {
    log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::ERROR, message);
}

void Logger::critical(const std::string& message) {
    log(LogLevel::CRITICAL, message);
}

// ==================== 带文件位置信息的日志方法 ====================

void Logger::trace(const std::string& message, const char* file, int line, const char* function) {
    if (LogLevel::TRACE >= current_level_) {
        logWithLocation(LogLevel::TRACE, message, file, line, function);
    }
}

void Logger::debug(const std::string& message, const char* file, int line, const char* function) {
    if (LogLevel::DEBUG >= current_level_) {
        logWithLocation(LogLevel::DEBUG, message, file, line, function);
    }
}

void Logger::info(const std::string& message, const char* file, int line, const char* function) {
    if (LogLevel::INFO >= current_level_) {
        logWithLocation(LogLevel::INFO, message, file, line, function);
    }
}

void Logger::warning(const std::string& message, const char* file, int line, const char* function) {
    if (LogLevel::WARNING >= current_level_) {
        logWithLocation(LogLevel::WARNING, message, file, line, function);
    }
}

void Logger::error(const std::string& message, const char* file, int line, const char* function) {
    if (LogLevel::ERROR >= current_level_) {
        logWithLocation(LogLevel::ERROR, message, file, line, function);
    }
}

void Logger::critical(const std::string& message, const char* file, int line, const char* function) {
    if (LogLevel::CRITICAL >= current_level_) {
        logWithLocation(LogLevel::CRITICAL, message, file, line, function);
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    if (level < current_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::string time_str = getCurrentTime();
    std::string level_str = getLevelString(level);
    
    if (color_enabled_) {
        const char* color = getLevelColor(level);
        std::cout << time_str << " " << color << level_str << LogColor::RESET 
                  << ": " << message << '\n';
    } else {
        std::cout << time_str << " " << level_str << ": " << message << '\n';
    }
}

std::string Logger::getCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    // auto timeinfo = *std::localtime(&time_t_now);
    struct tm timeinfo;
    localtime_r(&time_t_now, &timeinfo);
    
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARNING";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

const char* Logger::getLevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return LogColor::WHITE;
        case LogLevel::DEBUG:    return LogColor::BLUE;
        case LogLevel::INFO:     return LogColor::GREEN;
        case LogLevel::WARNING:  return LogColor::YELLOW;
        case LogLevel::ERROR:    return LogColor::RED;
        case LogLevel::CRITICAL: return LogColor::MAGENTA;
        default:                 return LogColor::RESET;
    }
}

// ==================== 带文件位置信息的日志输出 ====================

void Logger::logWithLocation(LogLevel level, const std::string& message, 
                            const char* file, int line, const char* function) {
    if (level < current_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    // 临时修复：强制输出位置信息，忽略 location_enabled_ 标志
    // TODO: 找到真正导致 location_enabled_ 被设置为 false 的原因
    
    std::string time_str = getCurrentTime();
    std::string level_str = getLevelString(level);
    std::string filename = extractFilename(file);
    
    // 构建位置信息字符串：filename:line (function)
    std::ostringstream location_ss;
    location_ss << filename << ":" << line;
    if (function && strlen(function) > 0) {
        location_ss << " (" << function << ")";
    }
    std::string location_str = location_ss.str();
    
    if (color_enabled_) {
        const char* color = getLevelColor(level);
        const char* loc_color = LogColor::CYAN;  // 位置信息用青色
        
        std::cout << time_str << " " 
                  << color << level_str << LogColor::RESET 
                  << " [" << loc_color << location_str << LogColor::RESET << "]"
                  << ": " << message << '\n';
    } else {
        std::cout << time_str << " " 
                  << level_str 
                  << " [" << location_str << "]"
                  << ": " << message << '\n';
    }
}

std::string Logger::extractFilename(const char* filepath) {
    if (!filepath) return "unknown";
    
    // 查找最后一个 '/' 或 '\' 字符
    const char* filename = filepath;
    const char* last_slash = strrchr(filepath, '/');
    if (last_slash) {
        filename = last_slash + 1;
    } else {
        last_slash = strrchr(filepath, '\\');
        if (last_slash) {
            filename = last_slash + 1;
        }
    }
    
    return std::string(filename);
}
