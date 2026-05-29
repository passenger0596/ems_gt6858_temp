#ifndef ACMETER_3366_H
#define ACMETER_3366_H

#include "device.h"

class ACMeter_3366 : public Device {
    public:
        ACMeter_3366(const std::string& name, int com, int id);
        ~ACMeter_3366()=default;

        void read_data(ModbusClient& mb_client) override;
        void parse_rawdata(const std::vector<uint16_t>& data_list) override;
        void init_config(const std::string& config_file) override;

    private:
        // 辅助函数
        // void init_json_structure(std::string name);
        void init_useful_indexes();

        // 寄存器段配置和缓冲区（优化性能，避免每次重新创建）
        // 使用父类 Device 中定义的 RegisterSegment 类型
        std::vector<Device::RegisterSegment> segments_;
        std::vector<std::vector<uint16_t>> data_buffer_vec_;
        
};




#endif
