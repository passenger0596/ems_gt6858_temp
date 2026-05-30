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

    init_useful_fc02_indexes();
    init_useful_fc04_indexes();

    this->alarm_bits.reserve(this->segments02_.size());
    this->data_buffer_vec02_.resize(this->segments02_.size());
    this->data_buffer_vec04_.resize(this->segments04_.size());
    
}

void GtBms::init_config(const std::string& config_file) {
    // 加载协议XML文件
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

    // 解析功能码02（离散输入 - 告警位）
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

                // 存储到映射和字典中
                this->fc02_nameToAddr_map[name] = address;
                this->data_dict_[name] = reg_data;
                this->dev_data_keys_.push_back(name);
                
                // 构建告警映射表（所有DI都作为告警处理，等级默认为2）
                alarm_map_.push_back({name, 2});
                
                LOG_DEBUG_LOC("加载离散输入[" + std::to_string(alarm_index) + "]: " + name + 
                             ", 地址: " + std::to_string(address));
                alarm_index++;
            }
        }
        LOG_INFO_LOC("加载离散输入(告警)数量: " + std::to_string(alarm_index));
    }

    // 解析功能码04（输入寄存器）
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

                // 存储到映射和字典中
                this->fc04_nameToAddr_map[name] = reg_data.address;
                this->data_dict_[name] = reg_data;
                this->dev_data_keys_.push_back(name);
                
                reg_count++;
            }
        }
        LOG_INFO_LOC("加载输入寄存器数量: " + std::to_string(reg_count));
    }

    // 初始化有用索引（用于快速访问）
    // // 对于gtBMS，我们直接使用dev_data_keys_的顺序，不需要特殊的索引映射
    // for (size_t i = 0; i < this->dev_data_keys_.size(); ++i) {
    //     this->useful_indexes.push_back(static_cast<uint16_t>(i));
    // }
    
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

        // 步骤1: 读取离散输入（告警位）- 功能码02
        // 读取97个离散输入位（需要13个字节，因为97/8 = 12.125，向上取整为13）
        std::vector<uint8_t> coil_buffer(13, 0);
        bool coil_read_success = mb_client.read_input_bits(1, 97, coil_buffer.data());
        for (size_t i=0; i<this->segments02_.size(); i++){
            const auto& segment02 = this->segments02_[i];
            this->data_buffer_vec02_[i].resize(segment02.num_regs);
            bool read_success = mb_client.read_holding_registers(
                segment.start_addr, segment.num_regs, this->data_buffer_vec_[i].data());
            
            if (!read_success) {
                total_read_success = false;
                break;
            }
        }
        
        if (!coil_read_success) {
            LOG_WARNING_LOC("读取离散输入(告警)失败: " + get_name());
            total_read_success = false;
        } else {
            // 解析离散输入数据
            parse_coil_data(coil_buffer);
        }

        // 步骤2: 读取输入寄存器 - 功能码04
        // 由于寄存器地址不连续（1-76, 500-511, 1000+），分段读取
        
        // 段1: 基础数据 (地址1-76)
        std::vector<uint16_t> base_buffer(76, 0);
        bool base_read_success = mb_client.read_input_registers(1, 76, base_buffer.data());
        
        if (!base_read_success) {
            LOG_WARNING_LOC("读取基础寄存器(1-76)失败: " + get_name());
            total_read_success = false;
        }

        // 段2: 箱温数据 (地址500-511, 共12个寄存器)
        std::vector<uint16_t> box_temp_buffer(12, 0);
        bool box_temp_read_success = mb_client.read_input_registers(500, 12, box_temp_buffer.data());
        
        if (!box_temp_read_success) {
            LOG_WARNING_LOC("读取箱温寄存器(500-511)失败: " + get_name());
            total_read_success = false;
        }

        // 段3: 单体电压 (地址1000开始，根据实际电池节数读取)
        // 假设最多150节电池，可根据实际情况调整
        const int max_cell_count = 150;
        std::vector<uint16_t> cell_voltage_buffer(max_cell_count, 0);
        bool cell_read_success = mb_client.read_input_registers(1000, max_cell_count, cell_voltage_buffer.data());
        
        if (!cell_read_success) {
            LOG_WARNING_LOC("读取单体电压寄存器(1000+)失败: " + get_name());
            total_read_success = false;
        }

        // 合并所有读取的数据
        if (total_read_success) {
            // 将三段数据合并到一个缓冲区进行解析
            std::vector<uint16_t> merged_data;
            
            // 添加基础数据（地址1-76）
            merged_data.insert(merged_data.end(), base_buffer.begin(), base_buffer.end());
            
            // 添加填充数据（地址77-499，这些地址不存在，用0填充以保持索引对应）
            merged_data.insert(merged_data.end(), 423, 0);  // 499 - 76 = 423
            
            // 添加箱温数据（地址500-511）
            merged_data.insert(merged_data.end(), box_temp_buffer.begin(), box_temp_buffer.end());
            
            // 添加填充数据（地址512-999）
            merged_data.insert(merged_data.end(), 488, 0);  // 999 - 511 = 488
            
            // 添加单体电压数据（地址1000+）
            merged_data.insert(merged_data.end(), cell_voltage_buffer.begin(), cell_voltage_buffer.end());
            
            // 解析合并后的数据
            parse_rawdata(merged_data);
            
            {
                std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
                this->data_to_qt["online_status"] = true;
            }
            this->online_status = true;
        } else {
            this->reconnect_counter++;
            if (this->reconnect_counter > 3) {
                {
                    std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
                    this->data_to_qt["online_status"] = false;
                }
                this->online_status = false;
                this->reconnect_counter = 0;
                LOG_ERROR_LOC("Modbus读取失败: " + get_name());
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

    } catch (const std::exception& e) {
        LOG_ERROR_LOC("Modbus读取异常 (" + get_name() + "): " + std::string(e.what()));
        {
            std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
            this->data_to_qt["online_status"] = false;
        }
        this->online_status = false;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void GtBms::parse_rawdata(const std::vector<uint16_t>& data_list) {
    int index = 0;
    json data_array = json::array();
    
    // ✅ 步骤1: 使用临时变量，无锁处理数据
    for (const auto& key : this->dev_data_keys_) {
        // 获取寄存器地址
        auto addr_it = this->fc04_nameToAddr_map.find(key);
        if (addr_it == this->fc04_nameToAddr_map.end()) {
            LOG_WARNING_LOC("未找到寄存器地址映射: " + key);
            continue;
        }
        
        uint16_t address = addr_it->second;
        
        // 检查地址是否在数据范围内
        if (address == 0 || address > data_list.size()) {
            LOG_WARNING_LOC("寄存器地址超出范围: " + key + ", 地址: " + std::to_string(address));
            continue;
        }
        
        // 注意：Modbus寄存器地址从1开始，数组索引从0开始
        uint16_t buffer_index = address - 1;
        
        // ✅ 线程安全地获取寄存器配置
        double mag = 1.0;
        uint16_t offset = 0;
        std::string datatype;
        
        if (!this->getRegisterConfig(key, mag, offset, datatype)) {
            LOG_WARNING_LOC("未找到寄存器配置: " + key);
            continue;
        }

        // 根据数据类型转换实际值
        double actual_value = 0.0;
        
        if (datatype.find("INT32") != std::string::npos || 
            datatype.find("UINT32") != std::string::npos) {
            // 32位数据需要两个寄存器
            if (buffer_index + 1 < data_list.size()) {
                actual_value = Utils::getUint32num(data_list[buffer_index], data_list[buffer_index + 1], 
                                                   Utils::Endian::BIG) / mag + offset;
            }
        } else if (datatype.find("INT16") != std::string::npos) {
            // 有符号16位整数
            actual_value = static_cast<int16_t>(data_list[buffer_index]) / mag + offset;
        } else {
            // 默认按无符号16位处理
            actual_value = static_cast<double>(data_list[buffer_index]) / mag + offset;
        }
        
        // ✅ 线程安全地更新寄存器值
        this->updateRegisterValue(key, actual_value);
        
        // 填充临时JSON数组
        data_array.push_back(actual_value);
        index++;
    }

    // ✅ 步骤2: 仅在最后原子替换时持锁
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");

    {
        std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
        this->data_to_qt["timestamp"] = ss.str();
        this->data_to_qt["online_status"] = true;
        this->data_to_qt["data"] = data_array;
    }  // 锁立即释放
    
    // 更新告警状态
    update_alarm_status();
}

void GtBms::parse_coil_data(const std::vector<uint8_t>& coil_data) {
    // 解析离散输入（告警位）
    // coil_data中的每个字节包含8个位
    int bit_index = 0;
    
    for (size_t byte_idx = 0; byte_idx < coil_data.size() && bit_index < 97; ++byte_idx) {
        uint8_t byte_val = coil_data[byte_idx];
        
        // 处理每个字节的8个位
        for (int bit = 0; bit < 8 && bit_index < 97; ++bit) {
            bool alarm_status = (byte_val >> bit) & 0x01;
            
            // 查找对应的告警名称
            if (bit_index < static_cast<int>(alarm_map_.size())) {
                const std::string& alarm_name = alarm_map_[bit_index].first;
                
                // 更新data_dict中的告警状态
                this->updateRegisterValue(alarm_name, alarm_status ? 1.0 : 0.0);
                
                // 同时更新data_to_qt
                {
                    std::unique_lock<std::shared_mutex> lock(this->data_to_qt_rwlock_);
                    this->data_to_qt[alarm_name] = alarm_status;
                }
            }
            
            bit_index++;
        }
    }
    
    LOG_DEBUG_LOC("解析离散输入告警位完成，共处理: " + std::to_string(bit_index) + " 位");
}

void GtBms::update_alarm_status() {
    // 基于离散输入的告警状态更新
    // 遍历所有告警位，调用基类的handle_alarm方法
    std::string now = Utils::getCurrentTimeString();
    
    for (const auto& alarm_pair : alarm_map_) {
        const std::string& alarm_name = alarm_pair.first;
        uint8_t level = alarm_pair.second;
        
        // 从data_dict获取告警状态
        double alarm_value = this->getValue<double>(alarm_name, 0.0);
        bool alarm_status = (alarm_value != 0.0);
        
        // 调用基类的通用告警处理方法
        this->handle_alarm(alarm_name, level, alarm_status, now);
    }
}

double GtBms::convert_raw_value(uint16_t raw_value, const RegisterData& reg_data) const {
    if (reg_data.datatype.find("INT16") != std::string::npos) {
        // 有符号16位整数
        return static_cast<int16_t>(raw_value) / reg_data.mag + reg_data.offset;
    } else {
        // 无符号处理
        return static_cast<double>(raw_value) / reg_data.mag + reg_data.offset;
    }
}


void GtBms::init_useful_fc02_indexes() {
    // 初始化02功能码的有用索引
    // 这里根据实际需求添加02功能码的有用索引
    // 例如：this->useful_indexes_fc02.push_back(1000);
    // 例如：this->useful_indexes_fc02.push_back(1001);
    LOG_DEBUG_LOC("高特BMS的fc02_nameToAddr_map大小: " + std::to_string(this->fc02_nameToAddr_map.size()));
    uint16_t j=0;
    for (uint8_t i =0;i<this->segments02_.size();++i){
        for (uint16_t index=this->segments02_[i].start_addr; index<this->segments02_[i].start_addr+this->segments02_[i].num_regs; ++index){
            for (const auto& pair : this->fc02_nameToAddr_map) {
                if (pair.second == index) {
                    this->useful_indexes_fc02.push_back(j);
                    break;
                }
            }
            ++j;
        }
    }

    // 确保索引数量与键数量一致（理想情况下）
    if (this->useful_indexes_fc02.size() != this->fc02_nameToAddr_map.size()) {
        LOG_WARNING_LOC("Warning: Useful indexes size (" + 
                       std::to_string(this->useful_indexes_fc02.size()) +
                       ") does not match data keys size (" + 
                       std::to_string(this->fc02_nameToAddr_map.size()) + ").");
        
        // 调试信息：显示缺失的寄存器
        LOG_DEBUG_LOC("调试信息 - 已找到索引的寄存器：");
        for (const auto& pair : this->fc02_nameToAddr_map) {
            LOG_DEBUG_LOC("  " + pair.first + " (地址: " + std::to_string(pair.second) + ")");
        }
    } else {
        LOG_INFO_LOC("高特BMS的fc02_nameToAddr_map索引匹配成功：" + std::to_string(this->useful_indexes_fc02.size()) + 
                    " 个寄存器已索引");
    }

}

void GtBms::init_useful_fc04_indexes() {
    // 初始化04功能码的有用索引
    // 这里根据实际需求添加04功能码的有用索引
    // 例如：this->useful_indexes_fc04.push_back(1000);
    // 例如：this->useful_indexes_fc04.push_back(1001);
    LOG_DEBUG_LOC("高特BMS的fc04_nameToAddr_map大小: " + std::to_string(this->fc04_nameToAddr_map.size()));
    uint16_t j=0;
    for (uint8_t i =0;i<this->segments04_.size();++i){
        for (uint16_t index=this->segments04_[i].start_addr; index<this->segments04_[i].start_addr+this->segments04_[i].num_regs; ++index){
            for (const auto& pair : this->fc04_nameToAddr_map) {
                if (pair.second == index) {
                    this->useful_indexes_fc04.push_back(j);
                    break;
                }
            }
            ++j;
        }
    }

    // 确保索引数量与键数量一致（理想情况下）
    if (this->useful_indexes_fc04.size() != this->fc04_nameToAddr_map.size()) {
        LOG_WARNING_LOC("Warning: Useful indexes size (" + 
                       std::to_string(this->useful_indexes_fc04.size()) +
                       ") does not match data keys size (" + 
                       std::to_string(this->fc04_nameToAddr_map.size()) + ").");
        
        // 调试信息：显示缺失的寄存器
        LOG_DEBUG_LOC("调试信息 - 已找到索引的寄存器：");
        for (const auto& pair : this->fc04_nameToAddr_map) {
            LOG_DEBUG_LOC("  " + pair.first + " (地址: " + std::to_string(pair.second) + ")");
        }
    } else {
        LOG_INFO_LOC("高特BMS的fc04_nameToAddr_map索引匹配成功：" + std::to_string(this->useful_indexes_fc04.size()) + 
                    " 个寄存器已索引");
    }

}
