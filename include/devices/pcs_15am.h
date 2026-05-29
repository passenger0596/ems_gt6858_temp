#ifndef PCS_H
#define PCS_H

#include "device.h"
#include <string>
#include <map>
#include <memory>
#include "json.hpp"
#include <vector>

using json = nlohmann::json;


class Pcs_15am : public Device {
public:
    Pcs_15am(const std::string& name, int com, int id);
    ~Pcs_15am()=default;
    
    void read_data(ModbusClient& mb_client) override;
    void parse_rawdata(const std::vector<uint16_t>& data_list) override;
    void init_config(const std::string& config_file) override;

private:   
    // 辅助函数
    // void init_json_structure(std::string name);
    void update_alarm_status();
    void init_useful_indexes();
    

    std::vector<Device::RegisterSegment> segments_;
    std::vector<std::vector<uint16_t>> data_buffer_vec_;

    std::vector<bool> alarm_bits;
    
    // 定义PCS可能的故障字寄存器列表（根据协议XML）
    std::vector<std::string> fault_words = {
        "硬件故障字1", "硬件故障字2", "电网故障字",
        "母线故障字", "交流电容故障字", "系统故障字",
        "开关故障字", "其他故障字"
    };
};

#endif