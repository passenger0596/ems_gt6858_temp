#ifndef UTILS_H
#define UTILS_H

#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <stdexcept>
#include <chrono>
#include <map>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <regex>

namespace Utils {

// 枚举定义
enum class Endian {
    BIG,
    LITTLE
};

enum class ByteOrder {
    ABCD,   // 标准Modbus: 高16位在前
    BADC,   // 字节内交换
    CDAB,   // 寄存器交换
    DCBA    // 全交换
};

// 结构体定义
struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second = 0;
};

struct Time {
    uint8_t hour;
    uint8_t minute;
};

// =============== 内联函数声明 ===============

// 字节序检测
inline bool is_big_endian() noexcept;

// 16位有符号数转无符号数
template<typename T>
inline typename std::enable_if<std::is_signed<T>::value && sizeof(T) <= 2, 
       typename std::make_unsigned<T>::type>::type
signed_to_unsigned(T num) noexcept;

// 16位无符号数转有符号数
template<typename T>
inline typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 2, 
       typename std::make_signed<T>::type>::type
unsigned_to_signed(T num) noexcept;

// 32位有符号数转无符号数
inline uint32_t signed_to_unsigned32(int32_t num) noexcept;

// 32位无符号数转有符号数
inline int32_t unsigned_to_signed32(uint32_t num) noexcept;

// 检查位是否设置
template<typename T>
inline bool is_bit_set(T value, int bit) noexcept;

// 设置位
template<typename T>
inline T set_bit(T value, int bit) noexcept;

// 清除位
template<typename T>
inline T clear_bit(T value, int bit) noexcept;

// 切换位
template<typename T>
inline T toggle_bit(T value, int bit) noexcept;

// 合并两个16位数
inline int32_t getInt32num(int16_t num1, int16_t num2, Endian endian) noexcept;
inline uint32_t getUint32num(uint16_t num1, uint16_t num2, Endian endian) noexcept;

// 拆分32位数
inline std::array<uint16_t, 2> splitUint32ToUint16(uint32_t num, Endian endian) noexcept;

// 合并四个16位数
inline int64_t getInt64num(int16_t num1, int16_t num2, int16_t num3, int16_t num4, Endian endian) noexcept;

// 交换字节序
template<typename T>
inline T swap_endian(T value) noexcept;

// 位操作相关
std::vector<bool> uint16_to_switches(uint16_t number, bool reverse_bits = false) noexcept;
std::vector<bool> uint8_to_switches(uint8_t number, bool reverse_bits = false) noexcept;
uint16_t switches_to_uint16(const std::vector<bool>& bits, bool reverse_bits = false);

// 字符串转换
inline Endian string_to_endian(const std::string& endian_str);
inline ByteOrder string_to_byteorder(const std::string& order_str);

// 时间处理
inline std::string getCurrentTimeString();
inline std::pair<uint8_t, uint8_t> time_str_to_seconds(const std::string& time_str);
inline Time parse_time(const std::string& time_str) noexcept;

// IP端口处理
inline std::pair<std::string, uint16_t> splitIpPort(const std::string& ip_port_str);

// 字典切片功能
template<typename K, typename V>
inline std::map<K, V> slice_dict(const std::map<K, V>& dictionary, int start, int end);

// 二进制转换
inline uint8_t binary_list_to_int(const std::vector<char>& binary_list) noexcept;
inline std::vector<char> int_to_binary_list(uint8_t num) noexcept;

// 安全格式化
inline std::string safe_format_datetime(uint16_t year, uint8_t month, uint8_t day,
                                        uint8_t hour = 0, uint8_t minute = 0, 
                                        uint8_t second = 0);

// =============== 非内联函数声明 ===============

// 寄存器与浮点数转换
float intRegisters_to_float(const std::vector<uint16_t>& registers, 
                           ByteOrder byteOrder = ByteOrder::ABCD);

std::vector<uint16_t> float_to_intRegisters(float num, 
                                          ByteOrder byteOrder = ByteOrder::ABCD);

// 解析日期时间
DateTime parse_datetime(const std::string& datetime_str);

// =============== 模板和内联函数实现 ===============

// 字节序检测
inline bool is_big_endian() noexcept {
    union {
        uint32_t i = 0x01020304;
        uint8_t c[4];
    } test = {};
    return test.c[0] == 0x01;
}

// 有符号数转无符号数 (模板版本)
template<typename T>
inline typename std::enable_if<std::is_signed<T>::value && sizeof(T) <= 2, 
       typename std::make_unsigned<T>::type>::type
signed_to_unsigned(T num) noexcept {
    return static_cast<typename std::make_unsigned<T>::type>(num);
}

// 无符号数转有符号数 (模板版本)
template<typename T>
inline typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 2, 
       typename std::make_signed<T>::type>::type
unsigned_to_signed(T num) noexcept {
    if (num < (1U << (sizeof(T) * 8 - 1))) {
        return static_cast<typename std::make_signed<T>::type>(num);
    } else {
        return static_cast<typename std::make_signed<T>::type>(
            num - (1U << (sizeof(T) * 8)));
    }
}

// 32位有符号数转无符号数
inline uint32_t signed_to_unsigned32(int32_t num) noexcept {
    return static_cast<uint32_t>(num);
}

// 32位无符号数转有符号数
inline int32_t unsigned_to_signed32(uint32_t num) noexcept {
    if (num < (1U << 31)) {
        return static_cast<int32_t>(num);
    } else {
        // 修复：使用uint64_t计算避免溢出
        return static_cast<int32_t>(num - 0x100000000LL);
    }
}

// 检查位是否设置
template<typename T>
inline bool is_bit_set(T value, int bit) noexcept {
    static_assert(std::is_integral<T>::value, "必须为整数类型");
    return (value >> bit) & 1;
}

// 设置位
template<typename T>
inline T set_bit(T value, int bit) noexcept {
    static_assert(std::is_integral<T>::value, "必须为整数类型");
    return value | (static_cast<T>(1) << bit);
}

// 清除位
template<typename T>
inline T clear_bit(T value, int bit) noexcept {
    static_assert(std::is_integral<T>::value, "必须为整数类型");
    return value & ~(static_cast<T>(1) << bit);
}

// 切换位
template<typename T>
inline T toggle_bit(T value, int bit) noexcept {
    static_assert(std::is_integral<T>::value, "必须为整数类型");
    return value ^ (static_cast<T>(1) << bit);
}

// 合并两个16位有符号数
inline int32_t getInt32num(int16_t num1, int16_t num2, Endian endian) noexcept {
    if (endian == Endian::BIG) {
        return (static_cast<int32_t>(num1) << 16) | (static_cast<int16_t>(num2) & 0xFFFF);
    } else {
        return (static_cast<int16_t>(num1) & 0xFFFF) | (static_cast<int32_t>(num2) << 16);
    }
}

// 合并两个16位无符号数
inline uint32_t getUint32num(uint16_t num1, uint16_t num2, Endian endian) noexcept {
    if (endian == Endian::BIG) {
        return (static_cast<uint32_t>(num1) << 16) | num2;
    } else {
        return num1 | (static_cast<uint32_t>(num2) << 16);
    }
}

// 拆分32位数
inline std::array<uint16_t, 2> splitUint32ToUint16(uint32_t num, Endian endian) noexcept {
    if (endian == Endian::BIG) {
        return {static_cast<uint16_t>((num >> 16) & 0xFFFF), 
                static_cast<uint16_t>(num & 0xFFFF)};
    } else {
        return {static_cast<uint16_t>(num & 0xFFFF),
                static_cast<uint16_t>((num >> 16) & 0xFFFF)};
    }
}

// 合并四个16位有符号数
inline int64_t getInt64num(int16_t num1, int16_t num2, int16_t num3, int16_t num4, 
                          Endian endian) noexcept {
    if (endian == Endian::BIG) {
        return (static_cast<int64_t>(num1) << 48) | 
               ((static_cast<int64_t>(num2) & 0xFFFF) << 32) |
               ((static_cast<int64_t>(num3) & 0xFFFF) << 16) |
               (static_cast<int16_t>(num4) & 0xFFFF);
    } else {
        return (static_cast<int16_t>(num1) & 0xFFFF) |
               ((static_cast<int64_t>(num2) & 0xFFFF) << 16) |
               ((static_cast<int64_t>(num3) & 0xFFFF) << 32) |
               (static_cast<int64_t>(num4) << 48);
    }
}

// 交换字节序
template<typename T>
inline T swap_endian(T value) noexcept {
    static_assert(std::is_trivially_copyable<T>::value, "必须为平凡可复制类型");
    static_assert(std::has_unique_object_representations<T>::value, 
                  "必须具有唯一对象表示");
    
    union {
        T val;
        uint8_t bytes[sizeof(T)];
    } src, dst;
    
    src.val = value;
    for (size_t i = 0; i < sizeof(T); i++) {
        dst.bytes[i] = src.bytes[sizeof(T) - 1 - i];
    }
    return dst.val;
}

// 字符串到字节序转换
inline Endian string_to_endian(const std::string& endian_str) {
    std::string lower_str = endian_str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    
    if (lower_str == "big") {
        return Endian::BIG;
    } else if (lower_str == "little") {
        return Endian::LITTLE;
    }
    throw std::invalid_argument("Invalid endian string, use 'big' or 'little'");
}

// 字符串到字节序顺序转换
inline ByteOrder string_to_byteorder(const std::string& order_str) {
    std::string upper_str = order_str;
    std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);
    
    if (upper_str == "ABCD") {
        return ByteOrder::ABCD;
    } else if (upper_str == "BADC") {
        return ByteOrder::BADC;
    } else if (upper_str == "CDAB") {
        return ByteOrder::CDAB;
    } else if (upper_str == "DCBA") {
        return ByteOrder::DCBA;
    }
    throw std::invalid_argument("Invalid byte order string, use 'ABCD', 'BADC', 'CDAB', or 'DCBA'");
}

// 获取当前时间字符串
inline std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 时间字符串转换
inline std::pair<uint8_t, uint8_t> time_str_to_seconds(const std::string& time_str) {
    std::regex pattern(R"((\d{1,2}):(\d{1,2}))");
    std::smatch matches;
    
    if (!std::regex_match(time_str, matches, pattern)) {
        throw std::invalid_argument("Invalid time format, expected hh:mm");
    }
    
    uint8_t hours = static_cast<uint8_t>(std::stoi(matches[1]));
    uint8_t minutes = static_cast<uint8_t>(std::stoi(matches[2]));
    
    if (hours > 23 || minutes > 59) {
        throw std::invalid_argument("Invalid time values");
    }
    
    return {hours, minutes};
}

// 解析时间
inline Time parse_time(const std::string& time_str) noexcept {
    try {
        auto [hour, minute] = time_str_to_seconds(time_str);
        return {hour, minute};
    } catch (...) {
        return {0, 0};
    }
}

// 分割IP端口
inline std::pair<std::string, uint16_t> splitIpPort(const std::string& ip_port_str) {
    size_t colon_pos = ip_port_str.find(':');
    if (colon_pos == std::string::npos) {
        throw std::invalid_argument("无效的IP:端口格式，缺少冒号分隔符");
    }
    
    std::string ip = ip_port_str.substr(0, colon_pos);
    if (ip.empty()) {
        throw std::invalid_argument("IP地址不能为空");
    }
    
    std::string port_str = ip_port_str.substr(colon_pos + 1);
    if (port_str.empty()) {
        throw std::invalid_argument("端口号不能为空");
    }
    
    try {
        int port_int = std::stoi(port_str);
        if (port_int <= 0 || port_int > 65535) {
            throw std::out_of_range("端口号超出有效范围（1-65535）");
        }
        uint16_t port = static_cast<uint16_t>(port_int);
        return {ip, port};
    } catch (const std::invalid_argument&) {
        throw std::invalid_argument("端口号不是有效的数字");
    } catch (const std::out_of_range&) {
        throw std::out_of_range("端口号超出整数范围");
    }
}

// 字典切片
template<typename K, typename V>
inline std::map<K, V> slice_dict(const std::map<K, V>& dictionary, int start, int end) {
    if (dictionary.empty()) {
        return {};
    }
    
    std::vector<K> keys;
    keys.reserve(dictionary.size());
    for (const auto& pair : dictionary) {
        keys.push_back(pair.first);
    }
    
    int len_keys = static_cast<int>(keys.size());
    
    // 处理负数索引
    start = start >= 0 ? start : len_keys + start;
    end = end >= 0 ? end : len_keys + end;
    
    // 确保索引在有效范围内
    start = std::max(0, std::min(start, len_keys - 1));
    end = std::max(0, std::min(end, len_keys - 1));
    
    if (start > end) {
        return {};
    }
    
    std::map<K, V> result;
    for (int i = start; i <= end; i++) {
        result[keys[i]] = dictionary.at(keys[i]);
    }
    return result;
}

// 二进制列表转整数
inline uint8_t binary_list_to_int(const std::vector<char>& binary_list) noexcept {
    uint8_t result = 0;
    for (size_t i = 0; i < binary_list.size() && i < 8; i++) {
        if (binary_list[i] == '1') {
            result |= (1U << i);
        }
    }
    return result;
}

// 整数转二进制列表
inline std::vector<char> int_to_binary_list(uint8_t num) noexcept {
    if (num > 127) {
        return {'1', '1', '1', '1', '1', '1', '1'};
    }
    
    std::string binary_str;
    for (int i = 6; i >= 0; i--) {
        binary_str += ((num >> i) & 1) ? '1' : '0';
    }
    
    std::vector<char> result(binary_str.begin(), binary_str.end());
    std::reverse(result.begin(), result.end());
    return result;
}

// 安全格式化日期时间
inline std::string safe_format_datetime(uint16_t year, uint8_t month, uint8_t day,
                                        uint8_t hour, uint8_t minute, uint8_t second) {
    // 基本验证
    if (year < 1000 || year > 9999) {
        throw std::invalid_argument("年份超出范围 (1000-9999)");
    }
    if (month < 1 || month > 12) {
        throw std::invalid_argument("月份超出范围 (1-12)");
    }
    if (day < 1 || day > 31) {
        throw std::invalid_argument("日期超出范围 (1-31)");
    }
    if (hour > 23) {
        throw std::invalid_argument("小时超出范围 (0-23)");
    }
    if (minute > 59) {
        throw std::invalid_argument("分钟超出范围 (0-59)");
    }
    if (second > 59) {
        throw std::invalid_argument("秒超出范围 (0-59)");
    }
    
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << year << "-"
        << std::setw(2) << std::setfill('0') << static_cast<int>(month) << "-"
        << std::setw(2) << std::setfill('0') << static_cast<int>(day) << " "
        << std::setw(2) << std::setfill('0') << static_cast<int>(hour) << ":"
        << std::setw(2) << std::setfill('0') << static_cast<int>(minute) << ":"
        << std::setw(2) << std::setfill('0') << static_cast<int>(second);
    
    return oss.str();
}

} // namespace Utils

#endif // UTILS_H