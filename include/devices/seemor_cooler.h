#ifndef SEEMOR_COOLER_H
#define SEEMOR_COOLER_H

#include "device.h"
#include <string>
#include <map>
#include <memory>
#include "json.hpp"
#include <vector>
#include <unordered_map>

using json = nlohmann::json;


class SeemorLiquidCooler : public Device {
public:
    SeemorLiquidCooler(const std::string& name, int com, int id);
    ~SeemorLiquidCooler()=default;
    
    void read_data(ModbusClient& mb_client) override;
    void parse_rawdata(const std::vector<uint16_t>& data_list) override;
    void init_config(const std::string& config_file) override;

private:   
    void update_alarm_status();

    std::vector<bool> alarm_bits;

    std::unordered_map<uint16_t, std::string> fault_words = {
        {3,"参数错误故障(控制板)"}, {4, "输入相序检测故障"}, {5, "三相检测缺相故障"},
        {6, "超速保护"}, {7, "回水温度传感器故障"}, {8, "出水温度传感器故障"},
        {9, "外环境温度传感器故障"}, {11, "排气温度传感器故障"},
        {15, "吸气温度传感器故障"}, {16, "485通讯故障"}, {17, "主板与驱动板通讯故障"},
        {18, "出水压力传感器故障"}, {19, "进水压力传感器故障"}, {24, "压机电流过流保护"},
        {25, "进出水压差过小保护"}, {26, "制冷出水温度过低保护"}, {29, "出水温度过高保护"},
        {30, "排气温度过高保护"}, {32, "高压传感器故障"}, {33, "低压传感器故障"},
        {36, "高压压力保护"}, {37, "低压压力保护"}, {38, "补水告警"},
        {39, "液位开关报警"}, {41, "直流风机1失速"}, {42, "出水压力过高报警"},
        {44, "驱动模块故障"}, {46, "直流母线电压超限"}, {47, "驱动模块温度超限"},
        {48, "压缩机启动异常"}, {49, "压缩机失步"}, {50, "驱动模块硬件瞬时过流"},
        {52, "驱动模块软件瞬时过流"}, {53, "整流侧硬件瞬时过流"},
        {55, "整流侧软件瞬时过流"}, {65, "水泵驱动故障"}
    };

    
};

#endif