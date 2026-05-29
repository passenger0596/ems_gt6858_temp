#include "utils.h"

namespace Utils {

// =============== 位操作相关函数 ===============

std::vector<bool> uint16_to_switches(uint16_t number, bool reverse_bits) noexcept {
    std::vector<bool> bits(16);
    for (int i = 0; i < 16; i++) {
        bits[i] = ((number >> i) & 1) == 1;
    }
    
    if (reverse_bits) {
        std::reverse(bits.begin(), bits.end());
    }
    return bits;
}

std::vector<bool> uint8_to_switches(uint8_t number, bool reverse_bits) noexcept {
    std::vector<bool> bits(8);
    for (int i = 0; i < 8; i++) {
        bits[i] = ((number >> i) & 1) == 1;
    }
    
    if (reverse_bits) {
        std::reverse(bits.begin(), bits.end());
    }
    return bits;
}

uint16_t switches_to_uint16(const std::vector<bool>& bits, bool reverse_bits) {
    if (bits.size() != 16) {
        throw std::invalid_argument("Bits list must contain exactly 16 elements");
    }
    
    std::vector<bool> processed_bits = bits;
    if (reverse_bits) {
        std::reverse(processed_bits.begin(), processed_bits.end());
    }
    
    uint16_t result = 0;
    for (size_t i = 0; i < processed_bits.size(); i++) {
        if (processed_bits[i]) {
            result |= (1U << i);
        }
    }
    return result;
}

// =============== 浮点数转换 ===============

float intRegisters_to_float(const std::vector<uint16_t>& registers, ByteOrder byteOrder) {
    if (registers.size() < 2) {
        throw std::invalid_argument("需要两个寄存器");
    }
    
    uint8_t reg0_high = static_cast<uint8_t>((registers[0] >> 8) & 0xFF);
    uint8_t reg0_low = static_cast<uint8_t>(registers[0] & 0xFF);
    uint8_t reg1_high = static_cast<uint8_t>((registers[1] >> 8) & 0xFF);
    uint8_t reg1_low = static_cast<uint8_t>(registers[1] & 0xFF);
    
    union {
        float f;
        uint8_t bytes[4];
    } result = {};
    
    switch (byteOrder) {
        case ByteOrder::ABCD:  // 标准Modbus: 高16位在前
            result.bytes[0] = reg0_high; 
            result.bytes[1] = reg0_low;
            result.bytes[2] = reg1_high; 
            result.bytes[3] = reg1_low;
            break;
        case ByteOrder::BADC:  // 字节内交换
            result.bytes[0] = reg0_low; 
            result.bytes[1] = reg0_high;
            result.bytes[2] = reg1_low; 
            result.bytes[3] = reg1_high;
            break;
        case ByteOrder::CDAB:  // 寄存器交换
            result.bytes[0] = reg1_high; 
            result.bytes[1] = reg1_low;
            result.bytes[2] = reg0_high; 
            result.bytes[3] = reg0_low;
            break;
        case ByteOrder::DCBA:  // 全交换
            result.bytes[0] = reg1_low; 
            result.bytes[1] = reg1_high;
            result.bytes[2] = reg0_low; 
            result.bytes[3] = reg0_high;
            break;
        default:
            throw std::invalid_argument("不支持的字节顺序");
    }
    
    // 如果系统是小端，需要转换字节序
    if (!is_big_endian()) {
        for (size_t i = 0; i < 2; i++) {
            std::swap(result.bytes[i], result.bytes[3 - i]);
        }
    }
    
    return result.f;
}

std::vector<uint16_t> float_to_intRegisters(float num, ByteOrder byteOrder) {
    union {
        float f;
        uint8_t bytes[4];
    } converter = {};
    
    converter.f = num;
    
    // 如果系统是小端，转换为大端存储
    if (!is_big_endian()) {
        for (size_t i = 0; i < 2; i++) {
            std::swap(converter.bytes[i], converter.bytes[3 - i]);
        }
    }
    
    uint16_t high_word, low_word;
    
    switch (byteOrder) {
        case ByteOrder::ABCD:  // 标准Modbus
            high_word = static_cast<uint16_t>((converter.bytes[0] << 8) | converter.bytes[1]);
            low_word = static_cast<uint16_t>((converter.bytes[2] << 8) | converter.bytes[3]);
            break;
        case ByteOrder::BADC:  // 字节内交换
            high_word = static_cast<uint16_t>((converter.bytes[1] << 8) | converter.bytes[0]);
            low_word = static_cast<uint16_t>((converter.bytes[3] << 8) | converter.bytes[2]);
            break;
        case ByteOrder::CDAB:  // 寄存器交换
            high_word = static_cast<uint16_t>((converter.bytes[2] << 8) | converter.bytes[3]);
            low_word = static_cast<uint16_t>((converter.bytes[0] << 8) | converter.bytes[1]);
            break;
        case ByteOrder::DCBA:  // 全交换
            high_word = static_cast<uint16_t>((converter.bytes[3] << 8) | converter.bytes[2]);
            low_word = static_cast<uint16_t>((converter.bytes[1] << 8) | converter.bytes[0]);
            break;
        default:
            throw std::invalid_argument("不支持的字节顺序");
    }
    
    return {high_word, low_word};
}

// =============== 日期时间解析 ===============

DateTime parse_datetime(const std::string& datetime_str) {
    std::regex pattern1(R"((\d{4})-(\d{1,2})-(\d{1,2}) (\d{1,2}):(\d{1,2}):(\d{1,2}))");
    std::regex pattern2(R"((\d{4})-(\d{1,2})-(\d{1,2}) (\d{1,2}):(\d{1,2}))");
    std::smatch matches;
    
    DateTime dt = {};
    
    if (std::regex_match(datetime_str, matches, pattern1)) {
        dt.year = static_cast<uint16_t>(std::stoi(matches[1]));
        dt.month = static_cast<uint8_t>(std::stoi(matches[2]));
        dt.day = static_cast<uint8_t>(std::stoi(matches[3]));
        dt.hour = static_cast<uint8_t>(std::stoi(matches[4]));
        dt.minute = static_cast<uint8_t>(std::stoi(matches[5]));
        dt.second = static_cast<uint8_t>(std::stoi(matches[6]));
    } else if (std::regex_match(datetime_str, matches, pattern2)) {
        dt.year = static_cast<uint16_t>(std::stoi(matches[1]));
        dt.month = static_cast<uint8_t>(std::stoi(matches[2]));
        dt.day = static_cast<uint8_t>(std::stoi(matches[3]));
        dt.hour = static_cast<uint8_t>(std::stoi(matches[4]));
        dt.minute = static_cast<uint8_t>(std::stoi(matches[5]));
    } else {
        throw std::invalid_argument("Invalid datetime format, expected YYYY-MM-DD HH:MM or YYYY-MM-DD HH:MM:SS");
    }
    
    // 基本验证
    if (dt.year < 1000 || dt.year > 9999 ||
        dt.month < 1 || dt.month > 12 || 
        dt.day < 1 || dt.day > 31 ||
        dt.hour > 23 || dt.minute > 59 || dt.second > 59) {
        throw std::invalid_argument("Invalid datetime values");
    }
    
    return dt;
}

} // namespace Utils