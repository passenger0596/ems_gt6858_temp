#ifndef DEHUMIFIER_V2_H
#define DEHUMIFIER_V2_H

#include "device.h"
#include <string>
#include <map>
#include <memory>
#include "json.hpp"
#include <vector>
#include <unordered_map>

using json = nlohmann::json;


class DehumidifierV2 : public Device {
public:
    DehumidifierV2(const std::string& name, int com, int id);
    ~DehumidifierV2() = default;

    void parse_rawdata(const std::vector<uint16_t>& data_list) override;
    void read_data(ModbusClient& mb_client) override;
    void init_config(const std::string& config_file) override;
    void init_useful_indexes();

private:
    std::vector<Device::RegisterSegment> segments_;
    std::vector<std::vector<uint16_t>> data_buffer_vec_;
};


#endif