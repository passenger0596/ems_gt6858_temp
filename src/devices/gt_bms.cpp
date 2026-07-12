#include "gt_bms.h"
#include "log.h"
#include "utils.h"
#include "config.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include "pugixml/pugixml.hpp"

GtBms::GtBms(const std::string& name, int com, int id) 
    : Device(name, com, id) {
    LOG_INFO_LOC("创建高特BMS设备: " + name + ", ID: " + std::to_string(id));
    this->name_ = name;
    this->id_ = id;
    this->com_ = com;
    Device::init_json_structure(name);
    init_config(Config::GTBMS_COMMUNICATION_FILEPATH);
    
    std::vector<uint16_t> addresses;
    for (const auto& pair : this->fc02_nameToAddr_map) {
        addresses.push_back(pair.second);
    }
    this->segments02_ = Device::generate_segments_from_addresses(addresses, 100);
    addresses.clear();
    for (const auto& pair : this->fc04_nameToAddr_map) {
        addresses.push_back(pair.second);
    }
    this->segments04_ = Device::generate_segments_from_addresses(addresses, 100);

    init_useful_indexes_from_map(this->fc02_nameToAddr_map, this->segments02_,
                                 parsed_registers_fc02_);
    init_useful_indexes_from_map(this->fc04_nameToAddr_map, this->segments04_,
                                 parsed_registers_fc04_);
    build_parsed_registers();

    this->alarm_bits.reserve(this->segments02_.size());
    this->data_buffer_vec02_.resize(this->segments02_.size());
    this->data_buffer_vec04_.resize(this->segments04_.size());
}

void GtBms::init_config(const std::string& config_file) {
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(config_file.c_str());
    
    if (!result) {
        LOG_ERROR_LOC("解析BMS协议XML文件失败: " + config_file + ", 错误: " + result.description());
        return;
    }

    pugi::xml_node root = doc.first_child();
    if (!root) {
        LOG_ERROR_LOC("XML根节点为空: " + config_file);
        return;
    }

    LOG_DEBUG_LOC("开始解析BMS协议: " + config_file);

    pugi::xml_node fc02_node = root.child("function_code02");
    if (fc02_node) {
        int alarm_index = 0;
        for (pugi::xml_node di : fc02_node.children("di")) {
            std::string name = di.attribute("name").as_string();
            uint16_t address = static_cast<uint16_t>(std::stoi(di.attribute("address").as_string()));
            
            if (!name.empty()) {
                RegisterData reg_data;
                reg_data.address = address;
                reg_data.mag = 1.0;
                reg_data.offset = 0;
                reg_data.datatype = "BOOL";
                reg_data.unit = "";
                reg_data.value = 0.0;

                this->fc02_nameToAddr_map[name] = address;
                this->data_dict_[name] = reg_data;
                this->dev_data_keys_.push_back(name);
                
                alarm_map_.push_back({name, 2});
                
                LOG_DEBUG_LOC("加载离散输入[" + std::to_string(alarm_index) + "]: " + name + 
                             ", 地址: " + std::to_string(address));
                alarm_index++;
            }
        }
        LOG_INFO_LOC("加载离散输入(告警)数量: " + std::to_string(alarm_index));
    }

    pugi::xml_node fc04_node = root.child("function_code04");
    if (fc04_node) {
        int reg_count = 0;
        for (pugi::xml_node ir : fc04_node.children("iRegister")) {
            std::string name = ir.attribute("name").as_string();
            if (!name.empty()) {
                RegisterData reg_data;
                reg_data.address = static_cast<uint16_t>(std::stoi(ir.attribute("address").as_string()));
                reg_data.mag = std::stod(ir.attribute("mag").as_string());
                reg_data.offset = static_cast<uint16_t>(std::stoi(ir.attribute("offset").as_string()));
                reg_data.datatype = ir.attribute("datatype").as_string();
                reg_data.unit = ir.attribute("unit").as_string();
                reg_data.value = 0.0;
                std::string endian_str = ir.attribute("endian").as_string("BIG");
                reg_data.big_endian = (endian_str != "LITTLE");
                if (reg_data.datatype.find("INT32") != std::string::npos ||
                    reg_data.datatype.find("UINT32") != std::string::npos) {
                    reg_data.register_count = 2;
                }

                this->fc04_nameToAddr_map[name] = reg_data.address;
                this->data_dict_[name] = reg_data;
                this->dev_data_keys_.push_back(name);
                
                reg_count++;
            }
        }
        LOG_INFO_LOC("加载输入寄存器数量: " + std::to_string(reg_count));
    }

    LOG_INFO_LOC("高特BMS配置初始化完成: " + get_name());
}

void GtBms::read_data(ModbusClient& mb_client) {
    if (!mb_client.is_connected()) {
        LOG_ERROR_LOC("ModbusClient未连接: " + get_name());
        return;
    }

    try {
        bool total_read_success = true;
        mb_client.set_slave(this->id_);

        // 读取离散输入(告警) - 基于段策略
        for (size_t i = 0; i < this->segments02_.size(); i++) {
            const auto& segment02 = this->segments02_[i];
            this->data_buffer_vec02_[i].resize(segment02.num_regs);
            bool read_success = mb_client.read_input_bits(
                segment02.start_addr, segment02.num_regs, this->data_buffer_vec02_[i].data());

            if (!read_success) {
                total_read_success = false;
                LOG_WARNING_LOC("读取离散输入段" + std::to_string(i) + "失败: " + get_name());
                break;
            }
        }

        if (total_read_success) {
            parse_di_data(this->data_buffer_vec02_, this->segments02_);
        }

        for (size_t i = 0; i < this->segments04_.size(); ++i) {
            const auto& segment = this->segments04_[i];
            this->data_buffer_vec04_[i].resize(segment.num_regs);
            bool read_success = mb_client.read_input_registers(
                segment.start_addr, segment.num_regs, this->data_buffer_vec04_[i].data());
            
            if (!read_success) {
                total_read_success = false;
                break;
            }
        }

        if (total_read_success) {
            this->data_buffer.clear();
            for (const auto& buf : this->data_buffer_vec04_) {
                this->data_buffer.insert(this->data_buffer.end(), buf.begin(), buf.end());
            }
            parse_rawdata(this->data_buffer);

            {
                std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
                this->data_to_qt["online_status"] = true;
            }
            this->online_status = true;
            this->reconnect_counter = 0;
        } else {
            this->reconnect_counter++;
            if (this->reconnect_counter > 3) {
                safe_set_qt_data(false);
                this->online_status = false;
                this->reconnect_counter = 0;
                LOG_ERROR_LOC("Modbus读取失败: " + get_name());
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Modbus读取异常 (" + get_name() + "): " + std::string(e.what()));
        safe_set_qt_data(false);
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void GtBms::parse_rawdata(const std::vector<uint16_t>& data_list) {
    this->online_status = true;
    json data_array = json::array();

    for (const auto& pr : parsed_registers_fc04_) {
        if (pr.buffer_index >= data_list.size()) break;

        double mag = 1.0;
        uint16_t offset = 0;
        std::string datatype;
        bool big_endian = true;
        uint8_t register_count = 1;

        if (!this->getRegisterConfig(pr.key, mag, offset, datatype, big_endian, register_count)) {
            data_array.push_back(0.0);
            continue;
        }

        double actual_value = parse_register_value(
            data_list, pr.buffer_index, mag, offset, datatype, big_endian, register_count);

        this->updateRegisterValue(pr.key, actual_value);
        data_array.push_back(actual_value);
    }

    safe_set_qt_data(true, data_array);
    
    update_alarm_status();
}

void GtBms::parse_di_data(const std::vector<std::vector<uint8_t>>& segment_buffers,
                             const std::vector<RegisterSegment>& segments) {
    // 从各段字节缓冲区中按地址提取位值,构建平坦位数组
    std::vector<bool> bit_values;
    for (size_t seg_idx = 0; seg_idx < segments.size(); ++seg_idx) {
        const auto& seg = segments[seg_idx];
        const auto& buf = segment_buffers[seg_idx];

        // 添加02功能码的值到bit_values
        for (const uint8_t &bit : buf) {
            bool bit_value = static_cast<bool>(bit);
            bit_values.push_back(bit_value);
        }


        // for (uint16_t addr = seg.start_addr; addr < seg.start_addr + seg.num_regs; ++addr) {
        //     uint16_t offset = addr - seg.start_addr;
        //     uint8_t byte_val = buf[offset / 8];
        //     bool bit_val = (byte_val >> (offset % 8)) & 0x01;
        //     bit_values.push_back(bit_val);
        // }
    }

    // 使用 parsed_registers_fc02_ 按 buffer_index 映射到寄存器名
    int processed = 0;
    for (const auto& pr : parsed_registers_fc02_) {
        if (pr.buffer_index >= bit_values.size()) {
            LOG_WARNING_LOC("离散输入位索引" + std::to_string(pr.buffer_index) +
                            " 超出数据范围 " + std::to_string(bit_values.size()));
            continue;
        }
        bool alarm_status = bit_values[pr.buffer_index];

        this->updateRegisterValue(pr.key, alarm_status ? 1.0 : 0.0);

        {
            // 02功能码是告警位，直接赋值QT的告警信息
            std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
            this->data_to_qt[pr.key] = alarm_status;
        }
        processed++;
    }

    LOG_DEBUG_LOC("解析离散输入告警位完成，共处理: " + std::to_string(processed) + " 位");
}

void GtBms::update_alarm_status() {
    for (const auto& alarm_pair : alarm_map_) {
        const std::string& alarm_name = alarm_pair.first;
        uint8_t level = alarm_pair.second;
        
        double alarm_value = this->getValue<double>(alarm_name, 0.0);
        bool alarm_status = (alarm_value != 0.0);
        
        this->handle_alarm(alarm_name, level, alarm_status);
    }
}

double GtBms::convert_raw_value(uint16_t raw_value, const RegisterData& reg_data) const {
    if (reg_data.datatype.find("INT16") != std::string::npos) {
        return static_cast<int16_t>(raw_value) / reg_data.mag + reg_data.offset;
    } else {
        return static_cast<double>(raw_value) / reg_data.mag + reg_data.offset;
    }
}