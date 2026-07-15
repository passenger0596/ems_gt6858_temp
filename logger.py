from loguru import logger
import os
import sys

# 配置日志目录（基于 logger.py 所在目录，不受 CWD 影响）
_LOG_BASE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(_LOG_BASE, 'logs')
if not os.path.exists(LOG_DIR):
    os.makedirs(LOG_DIR)

def configure_logging():
    # 移除默认的控制台输出
    logger.remove()

    # 保留你原有的「终端/非终端」判断逻辑（核心功能不丢）
    is_terminal = sys.stderr.isatty()

    # 1. 格式区分：保留你原有的两种格式逻辑
    if is_terminal:
        # 终端直接运行：带完整时间戳的格式
        console_format = "<level>{time:YYYY-MM-DD HH:mm:ss} | {name}:{line} | {message}</level>"
    else:
        # 非终端（systemd/journalctl）：简洁格式（省略自带时间戳）
        console_format = "<level>{name}:{line} | {message}</level>"

    # 2. 颜色控制：强制输出ANSI颜色码（让journalctl能识别）
    #    无论是否终端，都开启colorize（关键：保留颜色码）
    colorize = True

    # 配置运行相关日志（控制台输出）
    logger.add(
        sys.stderr,
        format=console_format,
        filter=lambda record: record["extra"].get("function") == "running",
        colorize=colorize,  # 强制开启颜色（输出ANSI码）
        enqueue=True        # 异步日志，避免阻塞
    )

    # 配置用户相关日志（控制台输出）
    logger.add(
        sys.stderr,
        format=console_format,
        filter=lambda record: record["extra"].get("function") == "user",
        colorize=colorize,
        enqueue=True
    )

    # 文件输出：强制关闭颜色（避免文件中写入ANSI码），格式不变
    logger.add(
        os.path.join(LOG_DIR, "running.log"),
        format="{time:YYYY-MM-DD HH:mm:ss}|{name}:{line}|{message}",
        filter=lambda record: record["extra"].get("function") == "running",
        rotation="100 MB",
        retention=10,
        encoding="utf-8",
        colorize=False  # 文件日志无颜色
    )

    logger.add(
        os.path.join(LOG_DIR, "user.log"),
        format="{time:YYYY-MM-DD HH:mm:ss}|{name}:{line}|{message}",
        filter=lambda record: record["extra"].get("function") == "user",
        rotation="100 MB",
        retention=10,
        encoding="utf-8",
        colorize=False
    )

    # 配置loguru的颜色级别（映射到ANSI码，确保颜色正确）
    logger.level("INFO", color="\033[36m")    # 青色
    logger.level("ERROR", color="\033[31m")   # 红色
    logger.level("WARNING", color="\033[33m") # 黄色
    logger.level("DEBUG", color="\033[32m")   # 绿色
    logger.level("CRITICAL", color="\033[35m")# 紫色

configure_logging()

# 创建绑定了功能标签的logger
user_logger = logger.bind(function="user")
running_logger = logger.bind(function="running")

# 测试代码（可选）
if __name__ == "__main__":
    user_logger.info("用户模块INFO日志（青色）")
    user_logger.error("用户模块ERROR日志（红色）")
    running_logger.info("运行模块INFO日志（青色）")
    running_logger.error("运行模块ERROR日志（红色）")