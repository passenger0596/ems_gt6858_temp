from flask import Flask, jsonify, request, Response, stream_with_context, send_file
from flask_cors import CORS
import redis
import json
import sqlite3
from datetime import datetime
import sys
import os
import io
import csv # 增加原生 csv 模块支持

# app.py / config.py / database_sqlalchemy.py 均在项目根目录，无需额外 sys.path
BASE_DIR = os.path.dirname(os.path.abspath(__file__))       # /opt/ems/
sys.path.insert(0, BASE_DIR)
from config import dev_nameToChinese_dict
from database_sqlalchemy import engine


app = Flask(__name__)
CORS(app)  # Enable CORS for all routes

# Initialize Redis connection
redis_conn = redis.Redis(host='localhost', port=6379, db=0, decode_responses=True)

@app.route('/api/devices', methods=['GET'])
def get_device_names():
    """获取所有设备名称的API"""
    try:
        # Get all keys that start with "device:"
        device_keys = redis_conn.keys("device:*")
        device_names = []
        
        for key in device_keys:
            device_name = key.split(":")[1]  # Extract device name from key like "device:pcs1"
            
            chinese_name = dev_nameToChinese_dict.get(device_name, device_name)
            device_names.append({
                'name': device_name,
                'chinese_name': chinese_name
            })
        
        return jsonify(device_names)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/devices/<device_name>', methods=['GET'])
def get_device_data(device_name):
    """获取指定设备的实时数据API"""
    try:
        key = f"device:{device_name}"
        data = redis_conn.get(key)
        
        if data:
            device_data = json.loads(data)
            return jsonify(device_data)
        else:
            return jsonify({'error': 'Device not found'}), 404
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/devices/<device_name>/alarms', methods=['GET'])
def get_device_alarms(device_name):
    """获取指定设备的告警信息API"""
    try:
        key = f"device:{device_name}"
        data = redis_conn.get(key)
        
        if data:
            device_data = json.loads(data)
            alarms = {
                'alarm1_status': device_data.get('alarm1_status', {}),
                'alarm2_status': device_data.get('alarm2_status', {}),
                'alarm3_status': device_data.get('alarm3_status', {}),
                'timestamp': device_data.get('timestamp'),
                'online_status': device_data.get('online_status')
            }
            return jsonify(alarms)
        else:
            return jsonify({'error': 'Device not found'}), 404
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/history/<device_name>', methods=['GET'])
def get_device_history(device_name):
    """从SQLite3数据库获取设备历史数据API"""
    try:
        # Get query parameters
        start_time = request.args.get('start_time')
        end_time = request.args.get('end_time')
        
        if not start_time or not end_time:
            return jsonify({'error': 'start_time and end_time parameters are required'}), 400
        
        # Parse time parameters - 直接使用前端发送的时间字符串，避免时区转换问题
        try:
            # 不进行datetime转换，直接使用字符串查询
            start_time_str = start_time
            end_time_str = end_time
        except Exception as e:
            return jsonify({'error': f'Invalid time format: {e}'}), 400
        
        # Use the database path from the existing engine
        db_path = engine.url.database
        
        # Connect to SQLite database and fetch data
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # Get table name based on device name
        table_name = device_name.lower()
        
        # Check if table exists
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name=?", (table_name,))
        if not cursor.fetchone():
            return jsonify({'error': f'Table {table_name} does not exist'}), 404
        
        # Query data within the specified time range - 直接使用字符串比较
        query = f"""
        SELECT * FROM "{table_name}" 
        WHERE timestamp BETWEEN ? AND ?
        ORDER BY timestamp ASC
        LIMIT 10000  -- Limit results to prevent huge responses
        """
        cursor.execute(query, (start_time_str, end_time_str))
        
        # Get column names
        columns = [description[0] for description in cursor.description]
        rows = cursor.fetchall()
        
        # Convert to list of dictionaries
        history_data = []
        for row in rows:
            row_dict = {}
            for i, col in enumerate(columns):
                row_dict[col] = row[i]
            history_data.append(row_dict)
        
        conn.close()
        
        return jsonify(history_data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500



@app.route('/api/export/csv', methods=['POST'])
def export_csv():
    try:
        data = request.json

        interval = data.get('interval', '15s')
        device_name = data.get('device_name')
        start_time = data.get('start_time')
        end_time = data.get('end_time')
        selected_points = data.get('selected_points', [])
        
        if not device_name or not start_time or not end_time:
            return jsonify({'error': '缺少必要参数'}), 400

        db_path = engine.url.database
        table_name = device_name.lower()

        # 1. 验证表是否存在，并获取数据库中真正存在的列
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name=?", (table_name,))
        if not cursor.fetchone():
            conn.close()
            return jsonify({'error': f'数据库中不存在设备表 {table_name}'}), 404

        cursor.execute(f"PRAGMA table_info('{table_name}')")
        all_cols_info = cursor.fetchall()
        valid_db_columns = [col[1] for col in all_cols_info] # 提取所有真实的列名
        conn.close()

        # 2. 核心修复：严格过滤 selected_points，剔除数据库中不存在的列
        if not selected_points:
            export_columns = [col for col in valid_db_columns if col not in ['id', 'online_status']]
        else:
            export_columns = ['timestamp']
            for p in selected_points:
                if p in valid_db_columns and p != 'timestamp':
                    export_columns.append(p)
                elif p not in valid_db_columns:
                    print(f"警告：前端请求的字段 '{p}' 在数据库表中不存在，已自动忽略。")

        # 3. 定义流式生成器
        def generate():
            try:
                # 写入 BOM 防止乱码
                yield '\ufeff'.encode('utf-8')
                
                output = io.StringIO()
                writer = csv.writer(output)
                
                # 写入表头
                writer.writerow(export_columns)
                yield output.getvalue().encode('utf-8')
                output.truncate(0)
                output.seek(0)

                # 根据选择的间隔构建过滤条件
                # SQLite 这里的逻辑是：只取秒数为 00 的行，或者分钟为整除的行
                interval_sql = ""
                if interval == '1m':
                    interval_sql = "AND strftime('%S', timestamp) = '00'"
                elif interval == '15m':
                    interval_sql = "AND strftime('%M', timestamp) IN ('00','15','30','45') AND strftime('%S', timestamp) = '00'"
                elif interval == '1h':
                    interval_sql = "AND strftime('%M', timestamp) = '00' AND strftime('%S', timestamp) = '00'"
                
                # 重新建立连接用于流式查询
                gen_conn = sqlite3.connect(db_path)
                gen_cursor = gen_conn.cursor()
                cols_query = ", ".join([f'"{c}"' for c in export_columns])
                query = f'SELECT {cols_query} FROM "{table_name}" WHERE timestamp BETWEEN ? AND ? {interval_sql} ORDER BY timestamp ASC'
                
                gen_cursor.execute(query, (start_time, end_time))
                
                while True:
                    rows = gen_cursor.fetchmany(100)
                    if not rows:
                        break
                    for row in rows:
                        writer.writerow(row)
                    
                    yield output.getvalue().encode('utf-8')
                    output.truncate(0)
                    output.seek(0)

                gen_conn.close()
                
            except Exception as e:
                # 终极保险：如果生成过程中崩溃，将报错信息作为字符串返回给前端
                # 这样浏览器不会报 Network Error，而是会下载一个包含报错提示的 CSV 文件
                error_msg = f"\n\n--- 导出在后端异常中断 ---\n错误详情: {str(e)}"
                print(error_msg)
                yield error_msg.encode('utf-8')

        filename = f"export_{device_name}.csv"
        return Response(
            stream_with_context(generate()),
            mimetype='text/csv',
            headers={
                "Content-Disposition": f"attachment; filename={filename}",
                "X-Accel-Buffering": "no", 
                "Cache-Control": "no-cache"
            }
        )

    except Exception as e:
        print(f"导出接口顶层异常: {str(e)}")
        return jsonify({'error': str(e)}), 500




@app.route('/api/devices/all', methods=['GET'])
def get_all_devices_data():
    """获取所有设备的实时数据API"""
    try:
        # Get all keys that start with "device:"
        device_keys = redis_conn.keys("device:*")
        all_devices_data = {}
        
        for key in device_keys:
            device_name = key.split(":")[1]  # Extract device name from key like "device:pcs1"
            data = redis_conn.get(key)
            
            if data:
                device_data = json.loads(data)
                all_devices_data[device_name] = device_data
        
        return jsonify(all_devices_data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500

# ======================== 控制 API ========================

FRONTEND_CONTROL_TOPIC = 'frontend/control'

# 加载控制命令中文名映射
_control_zh_map = {}
_control_zh_file = os.path.join(BASE_DIR, 'cfg', 'control_command_zh.json')
try:
    with open(_control_zh_file, 'r', encoding='utf-8') as f:
        _control_zh_map = json.load(f)
except Exception as e:
    print(f'[WARNING] 无法加载 control_command_zh.json: {e}')


def _get_zh_info(device, command):
    """获取控制命令的中文名和 dataKey"""
    dev_map = _control_zh_map.get(device, {})
    return dev_map.get(command, {})


@app.route('/api/control/commands', methods=['GET'])
def get_control_commands():
    """获取所有设备可用的控制命令列表（含中文名和当前值 key）"""
    try:
        # 使用 BASE_DIR 确保在任何目录启动都能找到 cfg/
        control_cmd_path = os.path.join(BASE_DIR, 'cfg', 'control_command.json')
        with open(control_cmd_path, 'r', encoding='utf-8') as f:
            control_dict = json.load(f)

        result = {}
        for device_name, commands in control_dict.items():
            chinese_name = dev_nameToChinese_dict.get(device_name, device_name)
            result[device_name] = {
                'chinese_name': chinese_name,
                'commands': []
            }
            for cmd_name, default_value in commands.items():
                zh_info = _get_zh_info(device_name, cmd_name)
                cmd_entry = {
                    'key': cmd_name,
                    'zh': zh_info.get('zh', cmd_name),
                    'dataKey': zh_info.get('dataKey'),
                    'type': zh_info.get('type') or _infer_value_type(default_value),
                    'group': zh_info.get('group', '其他'),
                    'multiCmd': zh_info.get('multiCmd'),
                    'confirm': zh_info.get('confirm', False),
                    'template': zh_info.get('template'),
                    'hide': zh_info.get('hide', False),
                    'default': default_value
                }
                result[device_name]['commands'].append(cmd_entry)

        return jsonify(result)
    except Exception as e:
        import traceback
        traceback.print_exc()
        return jsonify({'error': str(e)}), 500


@app.route('/api/control/values/<device_name>', methods=['GET'])
def get_control_values(device_name):
    """获取设备当前的控制参数实际值（从 Redis 实时数据中提取）"""
    try:
        key = f"device:{device_name}"
        raw = redis_conn.get(key)
        if not raw:
            return jsonify({'error': '设备不在线'}), 404

        device_data = json.loads(raw)
        data_dict = device_data.get('data', device_data)

        values = {}
        if device_name in _control_zh_map:
            for cmd_name, zh_info in _control_zh_map[device_name].items():
                data_key = zh_info.get('dataKey')
                if not data_key:
                    continue
                if data_key in data_dict:
                    item = data_dict[data_key]
                    values[cmd_name] = item.get('value') if isinstance(item, dict) else item
                elif data_key in device_data:
                    values[cmd_name] = device_data.get(data_key)
                elif data_key == 'timingModeSet':
                    values[cmd_name] = device_data.get('timingModeSet')
                elif data_key == 'demandResponseModeSet':
                    values[cmd_name] = device_data.get('demandResponseModeSet')

        return jsonify({'device': device_name, 'values': values, 'timestamp': device_data.get('timestamp')})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/control', methods=['POST'])
def send_control_command():
    """接收前端控制指令，发布到 Redis 控制频道"""
    try:
        data = request.json
        if not data:
            return jsonify({'error': '请求体为空'}), 400

        device = data.get('device', '')
        command = data.get('command', '')
        value = data.get('value')
        cmd_type = data.get('type', 'single')

        if not device or not command:
            return jsonify({'error': '缺少 device 或 command 参数'}), 400

        payload = {
            'device': device,
            'command': command,
            'value': value,
            'type': cmd_type
        }

        if cmd_type == 'multi':
            payload['items'] = data.get('items', [])

        if cmd_type == 'timing':
            payload['command'] = 'sys_setTimer'
            payload['type'] = 'single'
            payload['timingModeSet'] = value

        if cmd_type == 'demandResponse':
            payload['command'] = 'sys_setDemandResponse'
            payload['type'] = 'single'
            payload['demandResponseModeSet'] = value

        if command == 'sys_setTimer':
            payload['timingModeSet'] = data.get('timingModeSet')

        if command == 'sys_setDemandResponse':
            payload['demandResponseModeSet'] = data.get('demandResponseModeSet')

        count = redis_conn.publish(FRONTEND_CONTROL_TOPIC, json.dumps(payload))

        return jsonify({
            'success': True,
            'message': '指令已发送',
            'device': device,
            'command': command,
            'value': value,
            'subscribers': count
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500


def _infer_value_type(value):
    """推断控制命令的值类型和控件类型"""
    if isinstance(value, bool):
        return 'switch'
    elif isinstance(value, (int, float)):
        return 'number'
    elif value is None:
        return 'number'
    return 'text'


# ======================== 用户认证 API ========================

import hashlib, secrets, threading

_USERS_FILE = os.path.join(BASE_DIR, 'cfg', 'users.json')
_users_lock = threading.Lock()
_active_tokens = {}


def _load_users():
    with open(_USERS_FILE, 'r', encoding='utf-8') as f:
        return {k: v for k, v in json.load(f).items() if not k.startswith('_')}


def _save_users(users):
    with _users_lock:
        with open(_USERS_FILE, 'w', encoding='utf-8') as f:
            json.dump(users, f, ensure_ascii=False, indent=4)


@app.route('/api/auth/login', methods=['POST'])
def auth_login():
    """登录验证"""
    try:
        data = request.json or {}
        username = data.get('username', '').strip()
        password = data.get('password', '')
        if not username or not password:
            return jsonify({'success': False, 'error': '用户名和密码不能为空'}), 400

        users = _load_users()
        user = users.get(username)
        if not user:
            return jsonify({'success': False, 'error': '用户不存在'}), 401

        pw_hash = hashlib.sha256(password.encode()).hexdigest()
        if pw_hash != user.get('password_hash', ''):
            return jsonify({'success': False, 'error': '密码错误'}), 401

        token = secrets.token_hex(32)
        _active_tokens[token] = {'username': username, 'role': user.get('role', 'user')}
        return jsonify({'success': True, 'token': token, 'username': username, 'role': user.get('role', 'user')})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/auth/register', methods=['POST'])
def auth_register():
    """注册新用户（需管理员 token）"""
    try:
        token = request.headers.get('X-Auth-Token', '')
        session = _active_tokens.get(token, {})
        if session.get('role') != 'admin':
            return jsonify({'success': False, 'error': '仅管理员可创建用户'}), 403

        data = request.json or {}
        username = data.get('username', '').strip()
        password = data.get('password', '')
        if not username or not password:
            return jsonify({'success': False, 'error': '用户名和密码不能为空'}), 400
        if len(password) < 4:
            return jsonify({'success': False, 'error': '密码至少4位'}), 400

        users = _load_users()
        if username in users:
            return jsonify({'success': False, 'error': '用户名已存在'}), 409

        users[username] = {
            'password_hash': hashlib.sha256(password.encode()).hexdigest(),
            'role': data.get('role', 'user'),
            'created': datetime.now().strftime('%Y-%m-%d'),
        }
        _save_users(users)
        return jsonify({'success': True, 'message': f'用户 {username} 创建成功'})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/auth/change-password', methods=['POST'])
def auth_change_password():
    """修改密码（需登录）"""
    try:
        token = request.headers.get('X-Auth-Token', '')
        session = _active_tokens.get(token, {})
        username = session.get('username', '')
        if not username:
            return jsonify({'success': False, 'error': '未登录'}), 401

        data = request.json or {}
        old_pw = data.get('old_password', '')
        new_pw = data.get('new_password', '')
        if not old_pw or not new_pw:
            return jsonify({'success': False, 'error': '新旧密码不能为空'}), 400
        if len(new_pw) < 4:
            return jsonify({'success': False, 'error': '新密码至少4位'}), 400

        users = _load_users()
        user = users.get(username)
        if hashlib.sha256(old_pw.encode()).hexdigest() != user.get('password_hash', ''):
            return jsonify({'success': False, 'error': '旧密码错误'}), 401

        users[username]['password_hash'] = hashlib.sha256(new_pw.encode()).hexdigest()
        _save_users(users)
        return jsonify({'success': True, 'message': '密码修改成功'})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/auth/users/<username>', methods=['DELETE'])
def auth_delete_user(username):
    """删除用户（需管理员，不能删除自己）"""
    try:
        token = request.headers.get('X-Auth-Token', '')
        session = _active_tokens.get(token, {})
        if session.get('role') != 'admin':
            return jsonify({'success': False, 'error': '仅管理员可删除用户'}), 403
        if session.get('username') == username:
            return jsonify({'success': False, 'error': '不能删除自己'}), 400

        users = _load_users()
        if username not in users:
            return jsonify({'success': False, 'error': '用户不存在'}), 404

        del users[username]
        _save_users(users)
        return jsonify({'success': True, 'message': f'用户 {username} 已删除'})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/auth/users', methods=['GET'])
def auth_list_users():
    """列出所有用户（需管理员）"""
    try:
        token = request.headers.get('X-Auth-Token', '')
        session = _active_tokens.get(token, {})
        if session.get('role') != 'admin':
            return jsonify({'success': False, 'error': '仅管理员可查看'}), 403
        users = _load_users()
        result = [{'username': k, 'role': v.get('role', 'user'), 'created': v.get('created', '')}
                  for k, v in users.items()]
        return jsonify({'success': True, 'users': result})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/auth/logout', methods=['POST'])
def auth_logout():
    """退出登录"""
    try:
        token = request.headers.get('X-Auth-Token', '')
        _active_tokens.pop(token, None)
        return jsonify({'success': True})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


# ======================== 协议文档导出 ========================

@app.route('/api/protocol/xlsx', methods=['GET'])
def export_protocol_xlsx():
    """生成 Modbus TCP 协议 xlsx 文档并下载"""
    try:
        from generate_modbus_protocol_xlsx import generate_xlsx
        import tempfile

        tmp = tempfile.NamedTemporaryFile(suffix='.xlsx', delete=False)
        tmp.close()
        generate_xlsx(tmp.name)
        return send_file(tmp.name, as_attachment=True, download_name='modbus_tcp_protocol.xlsx',
                        mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet')
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/protocol/mqtt', methods=['GET'])
def export_mqtt_protocol_xlsx():
    """生成 MQTT 协议 xlsx 文档并下载"""
    try:
        import tempfile
        from generate_mqtt_protocol_xlsx import generate_xlsx
        tmp = tempfile.NamedTemporaryFile(suffix='.xlsx', delete=False)
        tmp.close()
        generate_xlsx(tmp.name)
        return send_file(tmp.name, as_attachment=True, download_name='mqtt_protocol.xlsx',
                        mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet')
    except Exception as e:
        return jsonify({'error': str(e)}), 500


# ======================== 历史告警 & 操作日志 API ========================

import sqlite3 as _sqlite3


def _dict_factory(cursor, row):
    """将 SQLite 行转换为字典"""
    return {col[0]: row[i] for i, col in enumerate(cursor.description)}


@app.route('/api/logs/alarms', methods=['GET'])
def get_alarm_history():
    """查询历史告警记录（支持时间范围和分页）"""
    try:
        start_time = request.args.get('start_time', '')
        end_time = request.args.get('end_time', '')
        level = request.args.get('level', '')
        device = request.args.get('device', '')
        page = int(request.args.get('page', 1))
        page_size = int(request.args.get('page_size', 50))

        # alarmHistory.db 位于项目根目录
        db_path = os.path.join(BASE_DIR, 'alarmHistory.db')
        if not os.path.exists(db_path):
            return jsonify({'records': [], 'total': 0, 'msg': f'数据库文件不存在: {db_path}'})

        conn = _sqlite3.connect(db_path)
        conn.row_factory = _dict_factory
        cursor = conn.cursor()

        where_clauses = []
        params = []

        if start_time:
            where_clauses.append('"告警时间" >= ?')
            params.append(start_time)
        if end_time:
            where_clauses.append('"告警时间" <= ?')
            params.append(end_time)
        if level:
            where_clauses.append('"等级" = ?')
            params.append(level)
        if device:
            where_clauses.append('"设备名称" LIKE ?')
            params.append(f'%{device}%')

        where_sql = ('WHERE ' + ' AND '.join(where_clauses)) if where_clauses else ''

        count_sql = f'SELECT COUNT(*) as cnt FROM alarmHistory {where_sql}'
        cursor.execute(count_sql, params)
        total = cursor.fetchone()['cnt']

        offset = (page - 1) * page_size
        query_sql = f'SELECT * FROM alarmHistory {where_sql} ORDER BY "告警时间" DESC LIMIT ? OFFSET ?'
        cursor.execute(query_sql, params + [page_size, offset])
        records = cursor.fetchall()

        conn.close()
        return jsonify({'records': records, 'total': total, 'page': page, 'page_size': page_size})
    except Exception as e:
        return jsonify({'error': str(e), 'records': [], 'total': 0}), 500


@app.route('/api/logs/operations', methods=['GET'])
def get_operation_logs():
    """查询操作日志（支持时间范围和分页）"""
    try:
        start_time = request.args.get('start_time', '')
        end_time = request.args.get('end_time', '')
        op_type = request.args.get('type', '')
        source = request.args.get('source', '')
        page = int(request.args.get('page', 1))
        page_size = int(request.args.get('page_size', 50))

        db_path = os.path.join(BASE_DIR, 'operation_log.db')
        if not os.path.exists(db_path):
            return jsonify({'records': [], 'total': 0})

        conn = _sqlite3.connect(db_path)
        conn.row_factory = _dict_factory
        cursor = conn.cursor()

        where_clauses = []
        params = []

        if start_time:
            where_clauses.append('timestamp >= ?')
            params.append(start_time)
        if end_time:
            where_clauses.append('timestamp <= ?')
            params.append(end_time)
        if op_type:
            where_clauses.append('type = ?')
            params.append(op_type)
        if source:
            where_clauses.append('source LIKE ?')
            params.append(f'%{source}%')

        where_sql = ('WHERE ' + ' AND '.join(where_clauses)) if where_clauses else ''

        count_sql = f'SELECT COUNT(*) as cnt FROM operation {where_sql}'
        cursor.execute(count_sql, params)
        total = cursor.fetchone()['cnt']

        offset = (page - 1) * page_size
        query_sql = f'SELECT id, strftime(\'%Y-%m-%d %H:%M:%S\', timestamp) as timestamp, type, source, desc FROM operation {where_sql} ORDER BY timestamp DESC LIMIT ? OFFSET ?'
        cursor.execute(query_sql, params + [page_size, offset])
        records = cursor.fetchall()

        conn.close()
        return jsonify({'records': records, 'total': total, 'page': page, 'page_size': page_size})
    except Exception as e:
        return jsonify({'error': str(e), 'records': [], 'total': 0}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)