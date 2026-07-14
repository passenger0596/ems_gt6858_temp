#include "acmeter_3366.h"

#include "pcs.h"
#include "dcdc.h"
#include "ems.h"
#include "ac_hengdu.h"
#include "gt_bms.h"  // 添加高特BMS头文件
#include "dg_hgm6100.h"
#include "infy_charger.h"
#include "increase_charger.h"
#include "iomodule.h"
#include "modbusserver.h"
#include <thread>
#include <atomic>
#include <functional>  // 用于 std::function
#include <vector>
#include <sw/redis++/redis++.h>  // redis-plus-plus 头文件

class ModbusClient;
class CanOperator;

class DeviceManager{
    public:
        DeviceManager();
        ~DeviceManager();
        std::vector<std::shared_ptr<Device>> devices_;
        std::unordered_map<std::string, std::shared_ptr<Device>> device_map_;   //  存储设备名称到设备指针的映射
        
        void createReadThreads();       // 创建modbus和can设备读取线程
        void stopAllThreads();          // 停止所有线程

        std::shared_ptr<Device> getDeviceByName(const std::string& name);       // 获取设备智能指针
        std::unordered_map<int, std::shared_ptr<ModbusClient>> getModbusClients();  // 获取modbus客户端智能指针
        std::unordered_map<int, std::shared_ptr<CanOperator>> getCanOperators();  // 获取can操作符智能指针
        
        void publishDataToRedis();  // 发布所有设备数据到Redis
        
        // 启动运行日志显示线程
        void startRunningLogThread();
        // 停止运行日志显示线程
        void stopRunningLogThread();

        // 新增：订阅云端控制消息
        void startSubscribeCloudControl();
        void stopSubscribeCloudControl();

        // 新增：数据库定时插入线程（从Redis获取数据，避免加锁）
        // 调用 startDbInserterThread() 启动，stopDbInserterThread() 停止
        // 不需要时注释掉 startDbInserterThread() 的调用即可
        void startDbInserterThread(int save_period_seconds = 15);
        void stopDbInserterThread();
        // 独立方法：从Redis获取所有设备数据并保存到数据库，可单独调用
        void saveDeviceDataFromRedis();
        
        // 新增：设置mqtt的控制消息回调函数
        using ControlMessageCallback = std::function<void(const std::string&, const std::string&)>;
        void setControlMessageCallback(ControlMessageCallback callback);



    private:
        std::unordered_map<uint8_t, std::vector<std::shared_ptr<Device>>> com_dev_map;  // 存储每个COM端口的设备列表
        std::unordered_map<uint8_t, std::vector<std::shared_ptr<Device>>> can_dev_map;  // 存储每个CAN口的设备列表
        std::vector<std::thread> device_threads_;

        // 具体设备实例
        std::shared_ptr<EMS> ems_ = nullptr;            // 恒天智信GT6856设备
        std::shared_ptr<Pcs> pcs_ = nullptr;            // 105kW PCS设备
        std::shared_ptr<Dcdc> dcdc1_ = nullptr;
        std::shared_ptr<Dcdc> dcdc2_ = nullptr;
        std::shared_ptr<ACMeter_3366> dtsd3366_ = nullptr;
        std::shared_ptr<GtBms> gt_bms_ = nullptr;  // 高特BMS设备
        
        std::shared_ptr<AcHengdu> ac_hengdu_ = nullptr;  // ACMeter_3366设备 
        std::shared_ptr<DgHgm6100n> dg_hgm6100_ = nullptr;  // DG_HGM6100设备

        std::shared_ptr<InfyCharger> chargers_ = nullptr;  // 英飞源充电器设备
        std::shared_ptr<IncreaseCharger> increase_charger_ = nullptr;  // 英可瑞充电器设备

        std::shared_ptr<IOModule> iomodule_ = nullptr;  // 中盛8DI 8DO数字量输入输出模块设备


        // 串口号与ModbusClient的映射
        std::unordered_map<int, std::shared_ptr<ModbusClient>> mapComToModbusClient;
        std::unordered_map<int, std::shared_ptr<CanOperator>> mapComToCanOperator;

        // 为每个设备开启独立的循环读取线程
        void readDeviceThreadWithStopFlag(uint8_t com, std::shared_ptr<ModbusClient> modbus_client, int thread_id);
        void readCanDeviceThreadWithStopFlag(uint8_t com, std::shared_ptr<CanOperator> can_operator, int thread_id);

        /**
         * @brief 为 CAN 设备开启独立的循环读取线程
         * @param interface_name 硬件接口名，如 "can0"
         * @param can_devices 该接口下的设备列表
         */
        void start_can_device_thread(const std::string& interface_name, 
                                    const std::vector<std::shared_ptr<Device>>& can_devices);

        /**
         * @brief CAN 设备轮询线程主函数
         */
        void can_device_loop(std::shared_ptr<CanOperator> can_operator, 
                            std::vector<std::shared_ptr<Device>> devices);

        std::atomic<bool> stop_threads_{false};

        // 添加新的控制机制
        std::unordered_map<int, std::atomic<bool>> thread_stop_flags_;
        std::unordered_map<int, std::shared_ptr<ModbusClient>> thread_modbus_clients_;
        std::unordered_map<int, std::shared_ptr<CanOperator>> thread_can_operators_;
        int next_thread_id_ = 0;
        
        // 运行日志显示线程
        std::thread running_log_thread_;
        std::atomic<bool> stop_running_log_{false};
        void runningLogShowThread();

        // 新增：订阅相关成员
        std::unique_ptr<sw::redis::Redis> redis_sub_client_;  // Redis订阅客户端
        std::unique_ptr<sw::redis::Subscriber> redis_subscriber_;  // Redis订阅者
        std::thread cloud_control_thread_;  // 云端控制订阅线程
        std::atomic<bool> stop_cloud_control_{false};  // 停止标志

        // 新增：控制消息回调,mqtt->redis->本地控制消息
        ControlMessageCallback control_message_callback_;

        // MQTT 配置（从 mqtt_config.json 加载）
        std::string mqtt_sn_;
        std::string mqtt_project_code_;
        std::string mqtt_control_channel_;
        void loadMqttConfig();

        // 新增：订阅线程函数
        void cloudControlSubscribeThread();

        // 新增：处理控制消息，mqtt->redis->本地控制消息
        void handleControlMessage(const std::string& channel, const std::string& message);

        // 新增：数据库定时插入线程
        std::thread db_inserter_thread_;
        std::atomic<bool> stop_db_inserter_{false};
        // 数据库插入线程主循环
        void dbInserterThreadLoop(int save_period_seconds);

        // ── Modbus TCP 服务器 ──
        struct Fc03Mapping {
            std::string device_name;  // 所属设备名
            std::string key;          // data_dict 中的 key
            double mag;
            uint16_t offset;
            std::string datatype;
            int reg_count;            // 1 or 2
            uint16_t rtu_addr;        // RTU原始modbus地址（EMS为0）
            uint16_t last_val[2];     // 上一次HR值，检测客户端写入
            int skip_count;           // RTU写入后跳过N轮推送等data_dict追上
            bool is_fc03_original = false;  // rtu_addr是否来自FC03地址空间（false则跳过RTU写回）
            uint8_t original_fc = 0;       // 原始功能码: 0=无, 1=FC01线圈, 3=FC03保持寄存器, 其他=只读不写回
            bool can_device_ctrl = false;  // true = 写入路由到CAN设备控制方法（而非RTU写回）
            bool writable = false;         // FC03 是否可写（false=只读映射）
        };

        std::unique_ptr<ModbusServer> modbus_tcp_server_;
        std::unique_ptr<ModbusServer> modbus_rtu_server_;
        std::thread modbus_sync_thread_;
        std::atomic<bool> modbus_sync_running_{false};

        // FC04: device_name → {start_addr, reg_count}
        std::unordered_map<std::string, std::pair<uint16_t, uint16_t>> fc04_offsets_;
        // FC04: 每设备自定义起始地址（未设置则自动按顺序分配）
        std::unordered_map<std::string, uint16_t> fc04_start_addrs_;
        // FC03: 每设备自定义起始地址（未设置则自动按顺序分配）
        std::unordered_map<std::string, uint16_t> fc03_start_addrs_;
        // FC03: tcp_addr → mapping entry (所有设备)
        std::map<uint16_t, Fc03Mapping> fc03_map_;
        uint16_t fc03_total_holding_ = 0;       // 最终03功能码区的映射数量
        uint16_t fc04_total_input_ = 0;         // 最终04功能码区的映射数量
        bool fc04_enabled_ = false;   // FC04默认关闭，通过setFc04Enabled启用

        // ── 定时模式 / 需求响应模式块映射 ──
        // 可配置的起始地址（可通过 setter 修改）
        uint16_t timer_block_start_addr_      = 200;
        uint16_t demand_block_start_addr_     = 400;

        // 动态条目数（根据 EMS 配置文件中的实际条目数决定）
        int timer_charge_entries_    = 0;
        int timer_discharge_entries_ = 0;
        int demand_entries_          = 0;

        static constexpr int TIMER_ENTRY_REGS     = 6;   // 每条定时记录用 6 个寄存器
        static constexpr int DEMAND_ENTRY_REGS    = 12;  // 每条需求响应记录用 12 个寄存器

        // 上一次写入的寄存器值，用于检测远程客户端变更
        std::vector<uint16_t> last_timer_block_;
        std::vector<uint16_t> last_demand_block_;

        // ── 定时/需求响应块同步方法 ──
        void syncTimerBlockTo(ModbusServer* server);     // 双向同步到指定server
        void syncDemandBlockTo(ModbusServer* server);
        void syncTimerBlock() { syncTimerBlockTo(modbus_tcp_server_.get()); }
        void syncDemandBlock() { syncDemandBlockTo(modbus_tcp_server_.get()); }

        void initModbusAddressMapping();                 // 公共映射逻辑（TCP/RTU共用）
        void initModbusTcpServer(const std::string& ip, int port);
        void initModbusRtuServer(const std::string& port, int baudrate, int slave_id);
        void syncAllFc04To(ModbusServer* server);
        void syncAllFc03To(ModbusServer* server);
        void syncAllFc04() { syncAllFc04To(modbus_tcp_server_.get()); }
        void syncAllFc03() { syncAllFc03To(modbus_tcp_server_.get()); }
        void modbusSyncLoop();
        static constexpr int FC03_SKIP_CYCLES = 4;  // RTU写入后跳过推送的轮数

    public:
        // 允许外部设置定时/需求响应块的起始地址（必须在 startModbusTcpServer 之前调用）
        inline void setTimerBlockStartAddr(uint16_t addr)  { timer_block_start_addr_ = addr; }
        inline void setDemandBlockStartAddr(uint16_t addr) { demand_block_start_addr_ = addr; }
        // 允许外部设置设备的 FC04 起始地址（必须在 startModbusTcpServer 之前调用）
        inline void setDeviceFc04StartAddr(const std::string& name, uint16_t addr) { fc04_start_addrs_[name] = addr; }
        // 允许外部设置设备的 FC03 起始地址（必须在 startModbusTcpServer 之前调用）
        inline void setDeviceFc03StartAddr(const std::string& name, uint16_t addr) { fc03_start_addrs_[name] = addr; }
        // 允许外部启用/禁用 FC04 输入寄存器映射（默认关闭，必须在 startModbusTcpServer 之前调用）
        inline void setFc04Enabled(bool enabled) { fc04_enabled_ = enabled; }

        // 启动 Modbus TCP 服务器
        void startModbusTcpServer();
        void stopModbusTcpServer();

        // 启动 Modbus RTU 服务器（RS485从站）
        void startModbusRtuServer();
        void stopModbusRtuServer();

};
