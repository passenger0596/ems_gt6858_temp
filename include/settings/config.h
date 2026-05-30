#ifndef CONFIG_H
#define CONFIG_H
#include <string>
#include <unordered_map>

namespace Config {
    const std::unordered_map<uint8_t, std::string> SERIAL_PORTS = {
        {0, "/dev/ttyS1"},
        {1, "/dev/ttyS10"},
        {2, "/dev/ttyS3"},
        {3, "/dev/ttyS2"},
        {4, "/dev/ttyS7"},
        {5, "/dev/ttyS6"},
        {6, "/dev/ttyS0"},
        {7, "/dev/ttyS8"},
        {8, "192.168.1.100:502"}

    };

    const std::unordered_map<uint8_t, std::string> CAN_INTERFACES = {
        {16, "can0"},
        {17, "can1"}
    };

    const std::string SOCKET_PATH = "/home/ubuntu/mnt/ems_project/ems_local.socket";;
    const std::string CONTROL_CMD_FILEPATH = "/home/ubuntu/mnt/ems_cpp/cfg/control_command.json";
    const std::string EMS_DATA_DICT_FILEPATH = "/home/ubuntu/mnt/ems_cpp/cfg/ems_data_dict.json";
    const std::string MQTT_CMD_FILEPATH = "/home/ubuntu/mnt/ems_cpp/cfg/mqtt_cmd.json";
    
    const std::string EMS_TCP_CMD_FILEPATH = "/home/ubuntu/mnt/ems_cpp/cfg/ems_tcp_cmd.json";
    const std::string EMS_CONFIG_FILEPATH_JSON = "/home/ubuntu/mnt/ems_cpp/cfg/ems_configure_param.json";

    const std::string EJDCDC_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/ejDCDC_protocol_V2.2.xml";
    const std::string EJPCS_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/ejPCS_protocol_V.1.24.xml";
    const std::string AC_METER_3366_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/DTSD3366D_protocol.xml";
    const std::string IOMODULE_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/zhongsheng_8di8do_protocol.xml";
    const std::string EMS_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/ems_dido.xml";
    const std::string EJPCS_15AM_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/EPCS15-AM_protocol_V.3.0.xml";
    const std::string DEHUMIDIFIER_V2_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/dehumidifier_protocol_V2.xml";
    const std::string WEA1610_COMMUNICATION_FILEPATH = "/home/ubuntu/mnt/ems_cpp/protocol/WEA1610_protocol.xml";
    const std::string BMS_UHOME_COMMUNICATION_FILEPATH = "/home/boring/code/cfg/bms_uhome_protocol.json";
    const std::string GTBMS_COMMUNICATION_FILEPATH = "/home/boring/code/cfg/gtBMS_485_protocol_V1.18.xml";


}

#endif // CONFIG_H
