#!/bin/bash

# Modbus Passthrough 测试脚本
# 使用前请确保已编译项目并连接好硬件

echo "=== Modbus Passthrough 测试脚本 ==="
echo ""

# 检查可执行文件是否存在
if [ ! -f "./modbus_passthrough_demo" ]; then
    echo "错误: 未找到 modbus_passthrough_demo 可执行文件"
    echo "请先编译项目: cd build && cmake .. && make"
    exit 1
fi

# 检查串口权限
echo "检查串口设备..."
if [ ! -e "/dev/ttyS1" ]; then
    echo "警告: /dev/ttyS1 不存在，请确认硬件连接"
fi

if [ ! -e "/dev/ttyS2" ]; then
    echo "警告: /dev/ttyS2 不存在，请确认硬件连接"
fi

echo ""
echo "启动透传服务..."
echo "提示: 按 Ctrl+C 停止服务"
echo ""

# 启动服务
sudo ./modbus_passthrough_demo
