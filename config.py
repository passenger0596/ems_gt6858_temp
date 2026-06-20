from multiprocessing import Queue
import os


# ubuntu环境的目录
EJPCS_COMMUNICATION_FILEPATH = './protocol/ejPCS_protocol_V.1.24.xml'
EJDCDC_COMMUNICATION_FILEPATH = './protocol/ejDCDC_protocol_V2.2.xml'
GTBMS_COMMUNICATION_FILEPATH = './protocol/gtBMS_protocol.xml'
SOCKET_PATH = './ems_local.socket'
SQLITE3_DATABASE_FILEPATH = './db/test.db'
KND4040_COMMUNICATION_FILEPATH = './protocol/kndIO4040_protocol.xml'
KND2080_COMMUNICATION_FILEPATH = './protocol/kndIO2080_protocol.xml'
DTSD_3366D_COMMUNICATION_FILEPATH = './protocol/DTSD3366D_protocol.xml'
GTBMS485_COMMUNICATION_FILEPATH = './protocol/gtBMS_485_protocol_V1.5.xml'
AC_COMMUNICATION_FILEPATH = './protocol/AC_hengdu_protocol_V1.3.xml'
SANHUA_COOLER_FILEPATH = './protocol/sanhua_cooler_protocol.xml'
LIQUID_COOLER_FILEPATH = './protocol/liquid_cooler_protocol.xml'
GTBMS485_V118_COMMUNICATION_FILEPATH = './protocol/gtBMS_485_protocol_V1.18.xml'
EPCS15_AM_COMMUNICATION_FILEPATH = './protocol/EPCS15-AM_protocol_V.3.0.xml'
WEA1610_FILEPATH = './protocol/WEA1610_protocol.xml'
BMS_UHOME_COMMUNICATION_FILEPATH = './protocol/uhomeBMS_protocol_v1.xml'
INFY_CHARGER_COMMUNICATION_FILEPATH = './protocol/infyCharger_protocol_V108.xml'
HGM6100N_COMMUNICATION_FILEPATH = './protocol/HGM6100N_protocol.xml'

GTBMS_PROTECTION_FILEPATH = './protocol/gtbms_protection.json'


EMS_CONFIG_FILEPATH = './cfg/ems_configure_param.json'  # 系统运行的配置参数json文件
EMS_DATA_DICT_FILEPATH = './cfg/ems_data_dict.json'
EMS_TCP_CMD_FILEPATH = './cfg/ems_tcp_cmd.json'
TEMP_FILEPATH = './cfg/temp.json'
CONTROL_COMMAND_FILEPATH = './cfg/control_command.json'
MQTT_CMD_FILEPATH = './cfg/mqtt_cmd.json'
MQTT_CMD_MAPPING_FILEPATH = './cfg/mqtt_cmd_mapping.json'
MQTT_CONTROL_TOPIC = 'cloud/action/xyc2026001/control'
FRONTEND_CONTROL_TOPIC = 'frontend/control'
IEC104_MAPPING_FILEPATH = './cfg/iec104_mapping.json'


TCP_DEVICE_IP = '192.168.1.98'      # modbus tcp设备ip
IEC104_SERVER_ENABLED = True
IEC104_SERVER_IP = '0.0.0.0'
IEC104_SERVER_PORT = 2404
IEC104_TICK_RATE_MS = 100
IEC104_SELECT_TIMEOUT_MS = 10000
IEC104_MAX_CONNECTIONS = 0
IEC104_SYNC_INTERVAL_MS = 1000
IEC104_MONITOR_IOA_BASE = 1
IEC104_CONTROL_IOA_BASE = 10000

# IEC104 总召唤分组 (按数据类型分配)
# Group 0 = 不参与总召唤, Group 1-16 = 总召唤分组
IEC104_INTERROGATION_GROUPS = {
    "BOOL": 1,       # 状态/开关量 -> Group 1
    "INT16": 2,      # 16位整型测量值 -> Group 2
    "UINT16": 2,
    "INT": 2,
    "INT32": 3,      # 32位计数器/累计量 -> Group 3
    "UINT32": 3,
    "FLOAT": 4,      # 浮点型精密测量值 -> Group 4
}

# IEC104 品质描述符: 设备离线后延迟多久设置 NotTopical (秒)，0=立即
IEC104_NOT_TOPICAL_DELAY_SEC = 30
CLUSTER_NUMBER = 1          # 电池簇数
PACK_NUMBER_PER_CLUSTER = 13           # 电池包数
CELL_NUM_PER_PACK = 16      # 电池电芯数
CELL_NUM_PER_CLUSTER = 208  # 电池簇电芯数

CHARGERS_NUM = 3        # 充电器数量

#   创建列表，初始化8个RS485串口，创建设备对象
EPC3568_SERIAL_LIST = [{'port': '/dev/ttyACM3', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyACM2', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS4', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS7', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS5', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS9', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyACM1', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyACM0', 'baudrate': 9600, 'type': 'master'}
                           ]

RK2676B_SERIAL_LIST = [ {'port': '/dev/ttyS1', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS10', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS3', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS2', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS7', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS6', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS0', 'baudrate': 9600, 'type': 'master'},
                           {'port': '/dev/ttyS8', 'baudrate': 9600, 'type': 'master'}
                           ]


# 待检查的TF卡数据库路径
CHECK_DB_PATH = '/home/ubuntu/mnt/ems_project/tfcard_db/storage_system.db'
# 默认本地数据库路径
DEFAULT_DB_PATH = './storage_system.db'
# DI/DO GPIO 编号字典已移除：此 EMS 无 DI/DO 硬件

# 判断TF卡路径是否存在（优先使用TF卡数据库）
# 检查目录是否存在且有写权限，而不是检查文件是否存在
tfcard_dir = os.path.dirname(CHECK_DB_PATH)

if not os.path.isdir(tfcard_dir):
    print(f"[WARNING] TF卡目录不存在: {tfcard_dir}")
    MAIN_DATABASE_FILEPATH = f'sqlite:///{DEFAULT_DB_PATH}'
elif not os.access(tfcard_dir, os.W_OK):
    # 获取目录所有者和权限信息
    import stat
    dir_stat = os.stat(tfcard_dir)
    owner_uid = dir_stat.st_uid
    current_uid = os.getuid()
    mode = stat.filemode(dir_stat.st_mode)
    print(f"[WARNING] TF卡目录无写权限:")
    print(f"  - 目录路径: {tfcard_dir}")
    print(f"  - 当前用户UID: {current_uid}")
    print(f"  - 目录所有者UID: {owner_uid}")
    print(f"  - 目录权限: {mode}")
    print(f"  - 修复建议: sudo chown -R ubuntu:ubuntu {tfcard_dir}")
    MAIN_DATABASE_FILEPATH = f'sqlite:///{DEFAULT_DB_PATH}'
else:
    MAIN_DATABASE_FILEPATH = f'sqlite:///{CHECK_DB_PATH}'
    print(f"[INFO] 使用TF卡数据库: {CHECK_DB_PATH}")

print(f"[INFO] 最终数据库路径: {MAIN_DATABASE_FILEPATH}")

# 全局队列，子进程接收到父进程读取或写的请求后反馈的结果队列
result_queue = Queue()

# modbus设备的共享写的数据队列字典，1个com对应1个队列
command_queues = {}

# 设备列表、设备名字列表
dev_list=[]
dev_name_list=[]
modbus_device_list = []
dev_dict={}

# 设备name和中文名字的映射字典
dev_nameToChinese_dict = {
    'gtbms485': 'bms',
    'pcs1': 'pcs',
    'dcdc1': 'dcdc1',
    'dcdc2': 'dcdc2',
    'ems': 'ems',
    'dtsd3366': '输入电表',
    'air_condition': '空调',
    'dg_hgm6100n': ' 柴油发电机',
    'chargers' : '充电机'

}





