cmake_minimum_required(VERSION 3.10)
project(ems_aarch64 LANGUAGES CXX)

# ==================== C++ 标准 ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==================== 编译选项 ====================
# 抑制 GCC 7.1 ABI 兼容性警告（来自 nlohmann/json 和 STL 容器）
add_compile_options(-Wno-psabi)
# Release 优化 + 保留少量调试信息
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g1 -DNDEBUG")
set(CMAKE_BUILD_TYPE Release)

# ==================== 交叉编译工具链 ====================
set(ARM_TOOLCHAIN_PREFIX "aarch64-none-linux-gnu")

set(CMAKE_C_COMPILER   ${ARM_TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${ARM_TOOLCHAIN_PREFIX}-g++)

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ==================== libmodbus 路径（开发机） ====================
set(MODBUS_ROOT $ENV{HOME}/aarch64-modbus)
set(LIBSOCKCANPP_ROOT $ENV{HOME}/libsockcanpp-aarch64)
set(HIREDIS_ROOT $ENV{HOME}/hiredis-aarch64)
set(SQLITE3_ROOT $ENV{HOME}/sqlite-aarch64)
set(SQLITECPP_ROOT $ENV{HOME}/sqlite-aarch64)
set(REDISPP_ROOT $ENV{HOME}/redis++-aarch64)

# ==================== 输出目录 ====================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# ==================== 可执行文件 ====================
# 方式一：使用 GLOB_RECURSE 自动查找所有源文件（不推荐，可能会有意外文件被包含）
# file(GLOB_RECURSE PROJECT_SOURCES 
#     "${CMAKE_SOURCE_DIR}/src/*.cpp"
# )

# 手动指定源文件列表，方便自定义编译哪些文件

# 主程序入口
set(MAIN_SOURCES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
)

# 设备驱动层
set(DEVICE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/devices/acmeter_3366.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/bms_uhome.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/device.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/devicemanager.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/dehumidifier_v2.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/dcdc.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/ems.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/gt_bms.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/iomodule.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/pcs.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/pcs_15am.cpp
    ${CMAKE_SOURCE_DIR}/src/devices/wea1610.cpp
)

# 控制策略层
set(CONTROL_SOURCES
    # ${CMAKE_SOURCE_DIR}/src/control/ejpcscmd.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ejpcs15amcmd.cpp
    ${CMAKE_SOURCE_DIR}/src/control/emscmd.cpp
    ${CMAKE_SOURCE_DIR}/src/control/mqttcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/control/qtcontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/control/strategy.cpp
)

# 工具类
set(UTILS_SOURCES
    ${CMAKE_SOURCE_DIR}/src/utils/canoperator.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/log.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/modbusclient.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/modbusserver.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/unixsocketserver.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/sqldatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/sqlcpp.cpp
)

# Modbus 透传功能（可选，注释掉则不编译）
# set(PASSTHROUGH_SOURCES
#     ${CMAKE_SOURCE_DIR}/src/utils/modbuspassthrough.cpp
# )

# 示例程序（默认不编译，需要时取消注释）
# set(EXAMPLE_SOURCES
#     ${CMAKE_SOURCE_DIR}/src/examples/modbus_passthrough_demo.cpp
#     ${CMAKE_SOURCE_DIR}/src/examples/modbus_passthrough_production.cpp
# )

# 添加 pugixml 源文件
# set(PUGIXML_SRC "/home/boring/arm-modbus/include/pugixml/src/pugixml.cpp")
set(PUGIXML_SRC  ${CMAKE_SOURCE_DIR}/src/pugixml/pugixml.cpp)

# 组合所有源文件（根据需要注释/取消注释某些模块）
set(PROJECT_SOURCES
    ${MAIN_SOURCES}
    ${DEVICE_SOURCES}
    ${CONTROL_SOURCES}
    ${UTILS_SOURCES}
    # ${PASSTHROUGH_SOURCES}      # 取消注释以启用透传功能
    # ${EXAMPLE_SOURCES}          # 取消注释以编译示例程序
    ${PUGIXML_SRC}
)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# ==================== ✅ 静态链接第三方库（推荐）====================
# 静态链接避免 GLIBC 版本兼容性问题
target_link_libraries(${PROJECT_NAME}
    ${LIBSOCKCANPP_ROOT}/lib/libsockcanpp.a
    ${MODBUS_ROOT}/lib/libmodbus.a      # 静态链接 libmodbus
    ${HIREDIS_ROOT}/lib/libhiredis.a    # 静态链接 hiredis
    ${SQLITE3_ROOT}/lib/libsqlite3.a    # 静态链接 SQLite3
    ${SQLITECPP_ROOT}/lib/libSQLiteCpp.a # 静态链接 SQLiteCpp
    ${REDISPP_ROOT}/lib/libredis++.a    # 静态链接 redis-plus-plus
    pthread
    m
)

# ==================== 可选：静态链接 C++ 运行库 ====================
# 如果仍担心 GLIBCXX 问题，可打开这一行
# target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++)

# ==================== 头文件 ====================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${LIBSOCKCANPP_ROOT}/include
    ${MODBUS_ROOT}/include
    ${SQLITE3_ROOT}/include             # SQLite3 头文件
    ${SQLITECPP_ROOT}/include           # SQLiteCpp 头文件
    ${REDISPP_ROOT}/include             # redis-plus-plus 头文件（根目录）
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/devices
    ${CMAKE_SOURCE_DIR}/include/nlohmann
    ${CMAKE_SOURCE_DIR}/include/pugixml
    ${CMAKE_SOURCE_DIR}/include/settings
    ${CMAKE_SOURCE_DIR}/include/utils
    ${CMAKE_SOURCE_DIR}/include/control
    ${HIREDIS_ROOT}/include             # hiredis 头文件
)

# ==================== 信息输出 ====================
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "libsockcanpp static lib: ${LIBSOCKCANPP_ROOT}/lib/libsockcanpp.a")
message(STATUS "Modbus static lib: ${MODBUS_ROOT}/lib/libmodbus.a")
message(STATUS "Hiredis static lib: ${HIREDIS_ROOT}/lib/libhiredis.a")
message(STATUS "SQLite3 static lib: ${SQLITE3_ROOT}/lib/libsqlite3.a")
message(STATUS "SQLiteCpp static lib: ${SQLITECPP_ROOT}/lib/libSQLiteCpp.a")
message(STATUS "Redis++ static lib: ${REDISPP_ROOT}/lib/libredis++.a")
message(STATUS "Output: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")