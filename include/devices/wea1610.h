#ifndef WEA1610_H
#define WEA1610_H

#include "device.h"
#include <string>
#include <map>
#include <memory>
#include "json.hpp"
#include <vector>
#include <unordered_map>

using json = nlohmann::json;


class Wea1610 : public Device {
public:
    Wea1610(const std::string& name, int com, int id);
    ~Wea1610() = default;

    void parse_rawdata(const std::vector<uint16_t>& data_list) override;
    void read_data(ModbusClient& mb_client) override;
    void init_config(const std::string& config_file) override;
    void init_useful_indexes();

private:
    std::vector<Device::RegisterSegment> segments_;
    std::vector<std::vector<uint16_t>> data_buffer_vec_;
};


#endif // WEA1610_H