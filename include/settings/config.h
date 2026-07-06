#ifndef CONFIG_H
#define CONFIG_H
#include <string>
#include <unordered_map>

namespace Config {
    // const std::unordered_map<uint8_t, std::string> SERIAL_PORTS = {
    //     {0, "/dev/ttyS1"},
    //     {1, "/dev/ttyS2"},
    //     {2, "/dev/ttyS3"},
    //     {3, "/dev/ttyS4"},
    //     {8, "192.168.1.100:502"}

    // };

    const std::unordered_map<uint8_t, std::string> SERIAL_PORTS = {
        {0, "/dev/ttyS3"},
        {1, "/dev/ttyS4"},
        {8, "192.168.1.100:502"}

    };

    const std::unordered_map<uint8_t, std::string> CAN_INTERFACES = {
        {16, "can0"},
        {17, "can1"}
    };

    const std::string SOCKET_PATH = "/opt/ems/ems_local.socket";;

    const std::string CONTROL_CMD_FILEPATH = "/opt/ems/cfg/control_command.json";

    const std::string EMS_DATA_DICT_FILEPATH = "/opt/ems/cfg/ems_data_dict.json";
    const std::string EMS_CONFIG_FILEPATH_JSON = "/opt/ems/cfg/ems_configure_param.json";

    const std::string MQTT_CMD_FILEPATH = "/opt/ems/cfg/mqtt_cmd.json";
    const std::string MQTT_CONFIG_FILEPATH = "/opt/ems/cfg/mqtt_config.json";

    

    const std::string EJDCDC_COMMUNICATION_FILEPATH = "/opt/ems/protocol/ejDCDC_protocol_V2.2.xml";
    const std::string EJPCS_COMMUNICATION_FILEPATH = "/opt/ems/protocol/ejPCS_protocol_V.1.24.xml";
    const std::string AC_METER_3366_COMMUNICATION_FILEPATH = "/opt/ems/protocol/DTSD3366D_protocol.xml";
    const std::string EJPCS_15AM_COMMUNICATION_FILEPATH = "/opt/ems/protocol/EPCS15-AM_protocol_V.3.0.xml";
    const std::string DEHUMIDIFIER_V2_COMMUNICATION_FILEPATH = "/opt/ems/protocol/dehumidifier_protocol_V2.xml";
    const std::string WEA1610_COMMUNICATION_FILEPATH = "/opt/ems/protocol/WEA1610_protocol.xml";
    const std::string BMS_UHOME_COMMUNICATION_FILEPATH = "/opt/ems/protocol/bms_uhome_protocol.json";
    const std::string GTBMS_COMMUNICATION_FILEPATH = "/opt/ems/protocol/gtBMS_485_protocol_V1.18.xml";
    const std::string AC_HENGDU_COMMUNICATION_FILEPATH = "/opt/ems/protocol/AC_hengdu_protocol_V1.3.xml";
    const std::string DG_HGM6100N_COMMUNICATION_FILEPATH = "/opt/ems/protocol/HGM6100N_protocol.xml";
    const std::string INFY_CHARGER_COMMUNICATION_FILEPATH = "/opt/ems/protocol/infyCharger_protocol_V108.xml";
    const std::string INCREASE_CHARGER_COMMUNICATION_FILEPATH = "/opt/ems/protocol/increaseCharger_protocol.xml";



}

#endif // CONFIG_H
