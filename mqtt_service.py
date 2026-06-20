import copy
import redis
import json
import paho.mqtt.client as mqtt
import time
import threading
import sys
import os
from datetime import datetime, timedelta
import gzip

# 扁平目录结构：所有 Python 模块在同一目录下
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from logger import running_logger
import config

# ==================== 加载 MQTT 配置 ====================

def _load_mqtt_config():
    """从 JSON 文件加载 MQTT 配置，换项目只需修改 cfg/mqtt_config.json"""
    cfg_path = './cfg/mqtt_config.json'
    try:
        with open(cfg_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except FileNotFoundError:
        running_logger.warning(f"MQTT 配置文件 {cfg_path} 未找到，使用默认值")
        return {}

_mqtt_cfg = _load_mqtt_config()

BROKER         = _mqtt_cfg.get('broker', 'localhost')
PORT           = _mqtt_cfg.get('port', 1883)
SN             = _mqtt_cfg.get('sn', 'default_sn')
PROJECT_CODE   = _mqtt_cfg.get('project_code', 'default_project')
MQTT_USERNAME  = _mqtt_cfg.get('username', '')
MQTT_PASSWORD  = _mqtt_cfg.get('password', '')
FULL_DATA_INTERVAL  = _mqtt_cfg.get('full_data_interval_s', 60)
DELTA_DATA_INTERVAL = _mqtt_cfg.get('delta_data_interval_s', 2.0)
COMPRESSION_ENABLED = _mqtt_cfg.get('compression_enabled', False)
COMPRESSION_LEVEL   = _mqtt_cfg.get('compression_level', 1)
INIT_FILE_PATH      = _mqtt_cfg.get('init_file_path', './mqtt_init.json')
EMS_SPECIAL_POINTS  = _mqtt_cfg.get('ems_special_points', {})

# 设备 MQTT 偏移和告警偏移（可从配置覆盖，也可自动推导）
_device_offsets = _mqtt_cfg.get('device_offsets', {})
_alarm_offsets  = _mqtt_cfg.get('alarm_offsets', {})

# ==================== 主题定义 ====================

REAL_DATA_TOPIC = f"cloud/push/{SN}/real"
CONTROL_TOPIC   = f"cloud/action/{SN}/control"
ACK_TOPIC       = f"cloud/action/{SN}/control/ack"

# ==================== 设备映射（从 config 读取，避免重复定义） ====================

# Redis key → MQTT 偏移
device_key = {}
for dev_name, zh_name in config.dev_nameToChinese_dict.items():
    offset = _device_offsets.get(dev_name, 0)
    redis_key = f"device:{dev_name}"
    device_key[redis_key] = offset

# 直接复用 config 的中文名映射
device_nameToChinese_dict = config.dev_nameToChinese_dict

# 告警偏移（当前未使用，保留供扩展）
alarm_offset = {}
for dev_name in config.dev_nameToChinese_dict:
    alarm_offset[dev_name] = _alarm_offsets.get(dev_name, 0)

# ==================== 全局状态 ====================

MQTT_DATA = {}
last_published_data = {}
last_full_publish_time = 0


def initialize_data_block() -> dict:
    try:
        with open(INIT_FILE_PATH, "r") as f:
            return json.load(f)
    except FileNotFoundError:
        running_logger.info('打开 mqtt 初始化文件失败')
        return {}


def compress_data(data_dict: dict) -> bytes:
    try:
        raw = json.dumps(data_dict, separators=(',', ':'), ensure_ascii=False)
        return gzip.compress(raw.encode('utf-8'), compresslevel=COMPRESSION_LEVEL)
    except Exception as e:
        running_logger.info(f"数据压缩错误: {e}")
        return json.dumps(data_dict, ensure_ascii=False).encode('utf-8')


# ==================== 数据解析（无副作用） ====================

def parse_data(device_data: dict, offset: int) -> list:
    """从设备 Redis 数据提取 MQTT 数据点列表（纯函数，无副作用）"""
    data_dict = device_data.get('data')
    if not data_dict:
        return []
    return [
        {'id': i + offset, 'value': data_dict[key]['value']}
        for i, key in enumerate(data_dict)
    ]


def parse_alarm(device_data: dict) -> list:
    """从设备 Redis 数据提取告警列表（循环处理1-3级，消除重复代码）"""
    alarm_list = []
    dev_name = device_nameToChinese_dict.get(device_data.get('name'), '')
    if not dev_name or device_data.get('online_status') == 0:
        return alarm_list

    TIME_FORMAT = "%Y-%m-%d %H:%M:%S"
    now = datetime.now()

    for level in range(1, 4):
        status_key = f"alarm{level}_status"
        alarms = device_data.get(status_key)
        if not alarms:
            continue
        for alarm_name, value in alarms.items():
            if not isinstance(value, dict):
                continue
            triggered = value.get('value', False)
            alarm_dict = {
                'level': level,
                'device': dev_name,
                'alarm': alarm_name,
                'lastTriggerTime': value.get('lastTriggerTime'),
                'lastClearTime': value.get('lastClearTime'),
            }
            if triggered:
                alarm_list.append(alarm_dict)
            else:
                # 恢复后24小时内仍上报
                last_clear = value.get('lastClearTime')
                if last_clear:
                    try:
                        clear_time = datetime.strptime(last_clear, TIME_FORMAT)
                        if now - clear_time <= timedelta(days=1):
                            alarm_list.append(alarm_dict)
                    except ValueError:
                        pass
    return alarm_list


def get_ems_special_points(ems_data: dict) -> list:
    """从 EMS 数据中提取特殊数据点（定时模式、需求响应等）"""
    extra = []
    for mid_str, field_name in EMS_SPECIAL_POINTS.items():
        val = ems_data.get(field_name)
        if val is not None:
            extra.append({'id': int(mid_str), 'value': val})
    return extra


def get_real_device_data(redis_conn) -> tuple[list, list, list]:
    """从 Redis 获取所有设备数据，返回 (数据点, 告警, 离线设备名列表)"""
    mqtt_data = []
    mqtt_alarm_data = []
    offline_devices = []

    for key, offset in device_key.items():
        dev_name = key.replace('device:', '')
        raw = redis_conn.get(key)
        if not raw:
            offline_devices.append(dev_name)
            continue
        dev_data = json.loads(raw)

        if dev_data.get('online_status') == 0:
            offline_devices.append(dev_name)
            continue

        data_list = parse_data(dev_data, offset)
        alarm_list = parse_alarm(dev_data)

        if 'ems' in key:
            data_list.extend(get_ems_special_points(dev_data))

        mqtt_data.extend(data_list)
        mqtt_alarm_data.extend(alarm_list)

    return mqtt_data, mqtt_alarm_data, offline_devices


# ==================== 增量/全量发布 ====================

def get_changed_data(current_data: list, online_devices: list) -> dict | None:
    """增量检测：返回变化的数据或全量数据"""
    global last_published_data, last_full_publish_time

    now_ts = time.time()
    is_full = (now_ts - last_full_publish_time >= FULL_DATA_INTERVAL)

    if is_full:
        running_logger.info(f"全量发布 (间隔 {FULL_DATA_INTERVAL}s)")
        return {
            'type': 'full',
            'online_devices': online_devices,
            'datetime': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            'timestamp': now_ts,
            'change_count': len(current_data),
            'data': current_data,
        }

    last_dict = {item['id']: item['value'] for item in last_published_data}
    changed = [item for item in current_data
               if last_dict.get(item['id']) != item['value']]

    if not changed:
        return None

    return {
        'type': 'changed',
        'online_devices': online_devices,
        'datetime': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        'timestamp': now_ts,
        'change_count': len(changed),
        'data': changed,
    }


# ==================== 发布线程 ====================

def publish_loop(client, topic, redis_conn):
    global last_full_publish_time, last_published_data

    if last_full_publish_time == 0:
        last_full_publish_time = time.time()

    while True:
        try:
            if not client.is_connected():
                time.sleep(DELTA_DATA_INTERVAL)
                continue

            current_data, alarm_data, offline_devs = get_real_device_data(redis_conn)
            offline_set = set(offline_devs)  # now all bare device names
            online_devs = [device_nameToChinese_dict.get(name, name)
                          for name in [k.replace('device:', '') for k in device_key]
                          if name not in offline_set]

            changed_info = get_changed_data(current_data, online_devs)

            if changed_info is None and not alarm_data:
                time.sleep(DELTA_DATA_INTERVAL)
                continue

            if alarm_data:
                if changed_info is not None:
                    changed_info['alarm_data'] = alarm_data
                else:
                    changed_info = {
                        'type': 'changed',
                        'online_devices': online_devs,
                        'datetime': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                        'timestamp': time.time(),
                        'change_count': 0,
                        'data': None,
                        'alarm_data': alarm_data,
                    }

            if changed_info['type'] == 'full':
                last_full_publish_time = changed_info['timestamp']

            publish_data = copy.deepcopy(changed_info)
            payload = (compress_data(publish_data) if COMPRESSION_ENABLED
                      else json.dumps(publish_data, ensure_ascii=False).encode('utf-8'))

            status = client.publish(topic, payload, qos=1)
            if status[0] == mqtt.MQTT_ERR_SUCCESS:
                running_logger.info(
                    f"{datetime.now():%Y-%m-%d %H:%M:%S}: "
                    f"{'全量' if changed_info['type'] == 'full' else '增量'}发布成功, "
                    f"变化: {changed_info['change_count']}, 在线设备: {online_devs}")
            else:
                running_logger.info(f'发布失败: {topic}')

            last_published_data = copy.deepcopy(current_data)

        except Exception as e:
            running_logger.info(f'发布错误: {e}')
        finally:
            time.sleep(DELTA_DATA_INTERVAL)


# ==================== 控制消息 ACK 回复 ====================

def _build_ack(status=1, error_msg="", payload=None):
    return {
        "type": "ack",
        "sn": SN,
        "code": PROJECT_CODE,
        "datetime": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "status": status,
        "error_msg": error_msg,
        "payload": payload or {},
    }


def _publish_ack(client, ack_data):
    ack_json = json.dumps(ack_data, ensure_ascii=False)
    status = client.publish(ACK_TOPIC, ack_json, qos=1)
    if status[0] == mqtt.MQTT_ERR_SUCCESS:
        running_logger.info(f"ACK 已发布: status={ack_data['status']}")
    else:
        running_logger.error(f"ACK 发布失败: {status[0]}")


# ==================== MQTT 客户端主入口 ====================

def start_mqtt_client():
    global MQTT_DATA
    try:
        redis_conn = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)
        redis_conn.ping()
        running_logger.info("Redis 连接成功")
    except Exception as e:
        running_logger.info(f"Redis 连接失败: {e}")
        return

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code == 0:
            running_logger.info(f"MQTT 已连接 (code={reason_code})")
            client.subscribe(CONTROL_TOPIC)
            running_logger.info(f"已订阅 {CONTROL_TOPIC}")
            mode = "压缩" if COMPRESSION_ENABLED else "明文"
            running_logger.info(f"发布模式: 全量{FULL_DATA_INTERVAL}s / 增量{DELTA_DATA_INTERVAL}s / {mode}")

    def on_message(client, userdata, msg):
        if msg.topic != CONTROL_TOPIC:
            return
        try:
            payload_str = msg.payload.decode()
            data = json.loads(payload_str)

            if data.get('sn') != SN or data.get('code') != PROJECT_CODE:
                ack = _build_ack(status=0, error_msg="SN/CODE 不匹配", payload=data)
                _publish_ack(client, ack)
                return

            ack = _build_ack(status=1, payload=data)
            running_logger.info(f"收到控制消息: {data}")
            serialized = json.dumps(data, ensure_ascii=False)
            redis_conn.setex(f"mqtt:{msg.topic}", 300, serialized)
            redis_conn.publish(CONTROL_TOPIC, serialized)

        except json.JSONDecodeError as e:
            ack = _build_ack(status=0, error_msg=f"JSON 解析失败: {e}", payload=msg.payload.decode())
        except Exception as e:
            ack = _build_ack(status=0, error_msg=f"处理错误: {e}")

        _publish_ack(client, ack)

    def on_subscribe(client, userdata, mid, reason_code_list, properties):
        running_logger.info(f"订阅确认 MID={mid}")

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        if reason_code != 0:
            running_logger.info(f"MQTT 断开 (code={reason_code})")
        else:
            running_logger.info("MQTT 正常断开")

    MQTT_DATA = initialize_data_block()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_subscribe = on_subscribe
    client.on_disconnect = on_disconnect

    try:
        client.connect(BROKER, PORT, 60)
        running_logger.info(f"连接 MQTT Broker {BROKER}:{PORT}")
        client.loop_start()

        t = threading.Thread(target=publish_loop, args=(client, REAL_DATA_TOPIC, redis_conn), daemon=True)
        t.start()
        running_logger.info("发布线程已启动")

        while True:
            if not client.is_connected():
                running_logger.info("MQTT 断连，尝试重连...")
                try:
                    client.reconnect()
                except Exception as e:
                    running_logger.info(f"重连失败: {e}")
            time.sleep(5)
    except KeyboardInterrupt:
        running_logger.info("MQTT 客户端关闭中...")
    except Exception as e:
        running_logger.info(f"MQTT 连接错误: {e}")
    finally:
        client.loop_stop()
        redis_conn.close()


if __name__ == '__main__':
    start_mqtt_client()
