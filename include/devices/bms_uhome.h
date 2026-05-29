#ifndef BMS_UHOME_H
#define BMS_UHOME_H

#include "device.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class BmsUhome
 * @brief Uhome BMS 设备实现类 (CAN 通讯)
 * 
 * 协议特点：
 * 1. 使用 CAN2.0B 扩展帧。
 * 2. 字段映射定义在 bms_uhome_protocol.json。
 * 3. 寄存器数据通过多帧连续发送，每帧携带 4 个寄存器 (8 字节)。
 */
class BmsUhome : public Device {
public:
    BmsUhome(const std::string& name, int com, int id);
    ~BmsUhome() = default;

    /**
     * @brief 加载协议 JSON 并初始化内部寄存器字典
     * @param config_file 协议文件路径
     */
    void init_config(const std::string& config_file) override;

    /**
     * @brief Modbus 读取入口 (BMS 暂不使用，留空实现)
     */
    void read_data(ModbusClient& mb_client) override;

    /**
     * @brief 解析原始字节流数据到 JSON 结构中
     * @param data_list 原始寄存器值列表 (16位)
     */
    void parse_rawdata(const std::vector<uint16_t>& data_list) override;

    /**
     * @brief 实现 CAN 读取逻辑：发送查询帧并接收多帧响应
     * @param can_operator 注入的 CAN 操作对象
     */
    void read_data(CanOperator& can_operator) override;

    /**
     * @brief 更新告警状态 (基于解析后的数据)
     */
    void update_alarm_status() override;

private:
    void init_default_can_mapping();
    uint16_t bytes_to_uint16(uint8_t high_byte, uint8_t low_byte) const;
    double convert_raw_value(uint16_t raw_value, const RegisterData& reg_data) const;
    // 所有读取帧的ID列表
    const std::vector<sockcanpp::CanId> response_ids_{0x351,0x355,0x356,0x359,0x35C,0x35F,0x399,
    0x770,0x771,0x772,0x773,0x774,0x775,0x776,0x777,0x778,0x779,0x77A,0x77B,0x7D0,0x7D1,0x7D2,0x7D3,0x7D4,0x7D5,
    0x7DA,0x7DB};
    std::unordered_map<uint32_t, size_t> response_id_to_index_;
    std::vector<uint16_t> register_cache_;
    sockcanpp::CanId request_id_ = 0;
    sockcanpp::CanId response_base_id_ = 0;
    int read_timeout_ms_ = 50;
    size_t registers_per_frame_ = 4;
};

#endif // BMS_UHOME_H
