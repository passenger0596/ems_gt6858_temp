import argparse
import json
import time
from datetime import datetime
import os
import sys
import redis


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--db", type=int, default=0)
    parser.add_argument("--pattern", default="device:*")
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--no-clear", action="store_true")
    return parser.parse_args()


def safe_json_load(text):
    try:
        return json.loads(text)
    except Exception:
        return None


def extract_value(item):
    if isinstance(item, dict) and "value" in item:
        return item.get("value")
    return item


def format_data_dict(data):
    if not isinstance(data, dict):
        return data, []
    keys = sorted(data.keys())
    lines = []
    for key in keys:
        value = extract_value(data[key])
        lines.append(f"{key}: {value}")
    return None, lines


def build_device_lines(payload):
    result = []
    name = payload.get("name", "unknown")
    online_status = payload.get("online_status")
    alarm1_status = payload.get("alarm1_status")
    alarm2_status = payload.get("alarm2_status")
    alarm3_status = payload.get("alarm3_status")
    timestamp = payload.get("timestamp")
    result.append(f"设备: {name}")
    result.append(f"在线状态: {online_status}  告警: L1={alarm1_status} L2={alarm2_status} L3={alarm3_status}  时间: {timestamp}")
    data = payload.get("data")
    raw_data, data_lines = format_data_dict(data)
    if data_lines:
        for line in data_lines:
            result.append(f"  {line}")
    else:
        result.append(f"  data: {raw_data}")
    result.append("")
    return result


def clear_screen():
    if os.name == "nt":
        os.system("cls")
    else:
        os.system("clear")


def setup_nonblocking_input():
    if os.name == "nt":
        return None
    try:
        import termios
        import tty
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        tty.setcbreak(fd)
        return old_settings
    except Exception:
        return None


def restore_input(old_settings):
    if not old_settings or os.name == "nt":
        return
    try:
        import termios
        termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_settings)
    except Exception:
        pass


def read_key():
    if os.name == "nt":
        try:
            import msvcrt
            if msvcrt.kbhit():
                return msvcrt.getwch()
            return None
        except Exception:
            return None
    try:
        import select
        if select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1)
        return None
    except Exception:
        return None


def wait_with_input(interval, paused_ref):
    remaining = interval
    while remaining > 0:
        key = read_key()
        if key in ("p", "P", " "):
            paused_ref[0] = not paused_ref[0]
            return
        if key in ("q", "Q"):
            paused_ref[1] = True
            return
        if key in ("/", "s", "S"):
            paused_ref[2] = True
            return
        time.sleep(0.1)
        remaining -= 0.1


def build_snapshot_lines(redis_conn, pattern):
    lines = [f"==== {datetime.now().strftime('%Y-%m-%d %H:%M:%S')} ===="]
    keys = sorted(redis_conn.scan_iter(match=pattern))
    if not keys:
        lines.append("未找到设备数据")
    for key in keys:
        raw = redis_conn.get(key)
        if not raw:
            continue
        payload = safe_json_load(raw)
        if not isinstance(payload, dict):
            lines.append(f"{key} 数据解析失败")
            continue
        lines.extend(build_device_lines(payload))
    lines.append("==================================================END==================================================")
    lines.append("按 p/空格 暂停或继续，按 q 退出，按 / 输入过滤")
    return lines


def apply_filter(lines, keyword):
    if not keyword:
        return lines
    lowered = keyword.lower()
    return [line for line in lines if lowered in line.lower()]


def prompt_filter(state):
    restore_input(state["input"])
    try:
        keyword = input("输入过滤关键字(空清除): ").strip()
    except Exception:
        keyword = ""
    state["input"] = setup_nonblocking_input()
    return keyword


def run_monitor(redis_conn, pattern, interval, once, no_clear, state):
    paused_state = [False, False, False]
    last_lines = []
    filter_keyword = ""
    while True:
        key = read_key()
        if key in ("p", "P", " "):
            paused_state[0] = not paused_state[0]
        elif key in ("q", "Q"):
            return
        elif key in ("/", "s", "S"):
            paused_state[2] = True
        if paused_state[0]:
            if paused_state[2]:
                filter_keyword = prompt_filter(state)
                paused_state[2] = False
                if not no_clear:
                    clear_screen()
                for line in apply_filter(last_lines, filter_keyword):
                    print(line)
            time.sleep(0.1)
            continue
        if not no_clear:
            clear_screen()
        last_lines = build_snapshot_lines(redis_conn, pattern)
        for line in apply_filter(last_lines, filter_keyword):
            print(line)
        if once:
            return
        wait_with_input(interval, paused_state)
        if paused_state[1]:
            return
        if paused_state[2]:
            paused_state[0] = True


def main():
    args = parse_args()
    redis_conn = redis.Redis(host=args.host, port=args.port, db=args.db, decode_responses=True)
    try:
        redis_conn.ping()
    except Exception as exc:
        print(f"Redis 连接失败: {exc}")
        return
    state = {"input": setup_nonblocking_input()}
    try:
        run_monitor(redis_conn, args.pattern, args.interval, args.once, args.no_clear, state)
    finally:
        restore_input(state["input"])


if __name__ == "__main__":
    main()
