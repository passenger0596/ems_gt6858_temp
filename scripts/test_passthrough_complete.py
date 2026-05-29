#!/usr/bin/env python3
"""
Modbus Passthrough 完整测试脚本
测试所有支持的功能码：01, 02, 03, 04, 05, 06, 0f, 10
"""

from pymodbus.client import ModbusTcpClient
import time

def test_registers(client):
    """测试寄存器相关功能码 (03, 04, 06, 10)"""
    print("\n=== Testing Registers ===")
    
    # 读取保持寄存器（功能码 03）
    print("\n1. Read Holding Registers (FC 03):")
    result = client.read_holding_registers(0, 10, slave=1)
    if not result.isError():
        print(f"   Addresses 0-9: {result.registers}")
    else:
        print(f"   Error: {result}")
    
    # 写入单个寄存器（功能码 06）
    print("\n2. Write Single Register (FC 06):")
    result = client.write_register(0, 1234, slave=1)
    if not result.isError():
        print(f"   Wrote 1234 to address 0")
        # 验证写入
        result = client.read_holding_registers(0, 1, slave=1)
        if not result.isError():
            print(f"   Verified: {result.registers[0]}")
    else:
        print(f"   Error: {result}")
    
    # 写入多个寄存器（功能码 16）
    print("\n3. Write Multiple Registers (FC 10):")
    values = [100, 200, 300, 400, 500]
    result = client.write_registers(1, values, slave=1)
    if not result.isError():
        print(f"   Wrote {values} to addresses 1-5")
        # 验证写入
        result = client.read_holding_registers(1, 5, slave=1)
        if not result.isError():
            print(f"   Verified: {result.registers}")
    else:
        print(f"   Error: {result}")

def test_coils(client):
    """测试线圈相关功能码 (01, 05, 0f)"""
    print("\n=== Testing Coils ===")
    
    # 读取线圈（功能码 01）
    print("\n1. Read Coils (FC 01):")
    result = client.read_coils(100, 16, slave=1)
    if not result.isError():
        print(f"   Addresses 100-115: {result.bits}")
    else:
        print(f"   Error: {result}")
    
    # 写入单个线圈（功能码 05）
    print("\n2. Write Single Coil (FC 05):")
    result = client.write_coil(100, True, slave=1)
    if not result.isError():
        print(f"   Wrote ON to address 100")
        # 验证写入
        result = client.read_coils(100, 1, slave=1)
        if not result.isError():
            print(f"   Verified: {'ON' if result.bits[0] else 'OFF'}")
    else:
        print(f"   Error: {result}")
    
    # 写入多个线圈（功能码 15）
    print("\n3. Write Multiple Coils (FC 0f):")
    values = [True, False, True, False, True, False, True, False]
    result = client.write_coils(101, values, slave=1)
    if not result.isError():
        print(f"   Wrote {values} to addresses 101-108")
        # 验证写入
        result = client.read_coils(101, 8, slave=1)
        if not result.isError():
            print(f"   Verified: {result.bits}")
    else:
        print(f"   Error: {result}")

def test_discrete_inputs(client):
    """测试离散输入功能码 (02)"""
    print("\n=== Testing Discrete Inputs ===")
    
    # 读取离散输入（功能码 02）
    print("\n1. Read Discrete Inputs (FC 02):")
    result = client.read_discrete_inputs(200, 16, slave=1)
    if not result.isError():
        print(f"   Addresses 200-215: {result.bits}")
    else:
        print(f"   Error: {result}")

def main():
    print("=" * 60)
    print("Modbus Passthrough Complete Test")
    print("=" * 60)
    
    # 连接到 Modbus TCP 服务器
    client = ModbusTcpClient('127.0.0.1', port=1502)
    
    if not client.connect():
        print("Error: Failed to connect to Modbus server")
        return
    
    print("\nConnected to Modbus server at 127.0.0.1:1502")
    
    try:
        # 测试寄存器
        test_registers(client)
        
        # 等待一下，让后台线程更新缓存
        time.sleep(1)
        
        # 测试线圈
        test_coils(client)
        
        # 等待一下
        time.sleep(1)
        
        # 测试离散输入
        test_discrete_inputs(client)
        
        print("\n" + "=" * 60)
        print("All tests completed!")
        print("=" * 60)
        
    except Exception as e:
        print(f"\nError during test: {e}")
    
    finally:
        client.close()
        print("\nConnection closed")

if __name__ == "__main__":
    main()
