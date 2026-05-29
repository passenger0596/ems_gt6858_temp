#include "acmeter_3366.h"
#include "pcs_15am.h"
#include "ems.h"
#include "bms_uhome.h"
#include "wea1610.h"
#include "dehumidifier_v2.h"
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
        
        void createReadThreads();
        void stopAllThreads();

        std::shared_ptr<Device> getDeviceByName(const std::string& name);
        std::unordered_map<int, std::shared_ptr<ModbusClient>> getModbusClients();
        std::unordered_map<int, std::shared_ptr<CanOperator>> getCanOperators();
        
        void publishDataToRedis();  // 发布所有设备数据到Redis
        
        // 启动运行日志显示线程
        void startRunningLogThread();
        // 停止运行日志显示线程
        void stopRunningLogThread();

        // 新增：订阅云端控制消息
        void startSubscribeCloudControl();
        void stopSubscribeCloudControl();
        
        // 新增：设置控制消息回调函数
        using ControlMessageCallback = std::function<void(const std::string&, const std::string&)>;
        void setControlMessageCallback(ControlMessageCallback callback);



    private:
        std::unordered_map<uint8_t, std::vector<std::shared_ptr<Device>>> dev_com_map;  // 存储每个COM端口的设备列表
        std::unordered_map<uint8_t, std::vector<std::shared_ptr<Device>>> can_dev_map;  // 存储每个CAN口的设备列表
        std::vector<std::thread> device_threads_;

        // 具体设备实例
        std::shared_ptr<EMS> ems_ = nullptr;
        std::shared_ptr<Pcs_15am> pcs_ = nullptr; 
        std::shared_ptr<ACMeter_3366> dtsd3366_ = nullptr;
        std::shared_ptr<BmsUhome> bms_uhome_ = nullptr;
        std::shared_ptr<Wea1610> wea1610_ = nullptr;
        std::shared_ptr<DehumidifierV2> dehumidifierV2_ = nullptr;


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
        
        // 新增：订阅线程函数
        void cloudControlSubscribeThread();
        
        // 新增：处理控制消息，mqtt->redis->本地控制消息
        void handleControlMessage(const std::string& channel, const std::string& message);

        // ── Modbus TCP 服务器 ──
        struct Fc03Mapping {
            std::string key;       // data_dict 中的 key
            double mag;
            uint16_t offset;
            std::string datatype;
            int reg_count;         // 1 or 2
            uint16_t last_val[2];  // 上一次同步后的值，用于检测外部写入
        };

        std::unique_ptr<ModbusServer> modbus_tcp_server_;
        std::thread modbus_sync_thread_;
        std::atomic<bool> modbus_sync_running_{false};

        // FC04: device_name → {start_addr, reg_count}
        std::unordered_map<std::string, std::pair<uint16_t, uint16_t>> fc04_offsets_;
        // FC04: 每设备自定义起始地址（未设置则自动按顺序分配）
        std::unordered_map<std::string, uint16_t> fc04_start_addrs_;
        // FC03: tcp_addr → EMS mapping entry
        std::map<uint16_t, Fc03Mapping> fc03_map_;

        // ── 定时模式 / 需求响应模式块映射 ──
        // 可配置的起始地址（可通过 setter 修改）
        uint16_t timer_block_start_addr_      = 100;
        uint16_t demand_block_start_addr_     = 200;

        // 最大条目数（决定了占用的寄存器空间）
        static constexpr int MAX_TIMER_ENTRIES   = 12;   // 12 × 6 = 72 寄存器
        static constexpr int MAX_DEMAND_ENTRIES  = 14;   // 14 × 12 = 168 寄存器

        static constexpr int TIMER_ENTRY_REGS     = 6;   // 每条定时记录用 6 个寄存器
        static constexpr int DEMAND_ENTRY_REGS    = 12;  // 每条需求响应记录用 12 个寄存器

        // 上一次写入的寄存器值，用于检测远程客户端变更
        std::vector<uint16_t> last_timer_block_;
        std::vector<uint16_t> last_demand_block_;

        // ── 定时/需求响应块同步方法 ──
        void syncTimerBlock();             // 双向同步: 检测外部写入→EMS, EMS→定时模式块
        void syncDemandBlock();            // 双向同步: 检测外部写入→EMS, EMS→需求响应块

        void initModbusTcpServer(const std::string& ip, int port);
        void syncAllFc04();
        void syncAllFc03();
        void modbusSyncLoop();

    public:
        // 允许外部设置定时/需求响应块的起始地址（必须在 startModbusTcpServer 之前调用）
        inline void setTimerBlockStartAddr(uint16_t addr)  { timer_block_start_addr_ = addr; }
        inline void setDemandBlockStartAddr(uint16_t addr) { demand_block_start_addr_ = addr; }
        // 允许外部设置设备的 FC04 起始地址（必须在 startModbusTcpServer 之前调用）
        inline void setDeviceFc04StartAddr(const std::string& name, uint16_t addr) { fc04_start_addrs_[name] = addr; }

    public:
        void startModbusTcpServer();
        void stopModbusTcpServer();

};
