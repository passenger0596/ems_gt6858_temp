#ifndef GT_BMS_H
#define GT_BMS_H

#include "device.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @class GtBms
 * @brief 高特BMS设备实现类 (Modbus RTU over RS485)
 * 
 * 协议特点：
 * 1. 使用 Modbus RTU或TCP 协议，功能码02(离散输入)和04(输入寄存器)
 * 2. 字段映射定义在 gtBMS_485_protocol_V1.18.xml
 * 3. 包含97个告警位、基础电池数据、箱温数据、大量单体电压数据
 * 4. 寄存器地址分布：
 *    - DI(地址1-97): 各级报警状态
 *    - IR(地址1-76): 簇电压/电流/SOC/SOH、温度统计等
 *    - IR(地址500-511): 12个电池箱温度
 *    - IR(地址1000+): 单体电压数据（可能超过100节）
 */
class GtBms : public Device {
public:
    /** 
     * @brief 构造函数
     * @param name 设备名称
     * @param com 串口号
     * @param id Modbus从站地址
     */
    GtBms(const std::string& name, int com, int id);
    ~GtBms() = default;

    /**
     * @brief 加载协议XML并初始化内部寄存器字典
     * @param config_file 协议文件路径
     */
    void init_config(const std::string& config_file) override;

    /**
     * @brief Modbus读取入口：读取离散输入和输入寄存器
     * @param mb_client Modbus客户端对象
     */
    void read_data(ModbusClient& mb_client) override;

    /**
     * @brief 解析原始寄存器数据到JSON结构中
     * @param data_list 原始寄存器值列表 (16位)
     */
    void parse_rawdata(const std::vector<uint16_t>& data_list) override;

    /**
     * @brief 更新告警状态（基于离散输入数据）
     */
    void update_alarm_status() override;

private:
    /**
     * @brief 转换原始值为实际物理值
     * @param raw_value 原始寄存器值
     * @param reg_data 寄存器配置信息
     * @return 转换后的实际值
     */
    double convert_raw_value(uint16_t raw_value, const RegisterData& reg_data) const;

    /**
     * @brief 解析离散输入告警位
     * @param coil_data 离散输入数据（每个元素代表一个位）
     */
    void parse_di_data(const std::vector<std::vector<uint8_t>>& segment_buffers,
                         const std::vector<RegisterSegment>& segments);

    // 告警映射表：索引 -> (告警名称, 告警等级)
    std::vector<std::pair<std::string, uint8_t>> alarm_map_;


    std::vector<bool> alarm_bits;
};

#endif // GT_BMS_H