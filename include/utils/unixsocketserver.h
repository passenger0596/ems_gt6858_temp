// UnixSocketServer.h
#ifndef UNIXSOCKETSERVER_H
#define UNIXSOCKETSERVER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

// 前向声明
class Device;

/**
 * @brief Unix Socket服务器类，用于在本地后端和QT/C++前端之间传输JSON数据
 */
class UnixSocketServer {
    public:
        /**
         * @brief 构造函数
         * @param socketPath Unix Socket文件路径
         * @param devList 设备列表的拷贝（持有所有权）
         */
        UnixSocketServer(const std::string& socketPath, 
                        const std::vector<std::shared_ptr<Device>> devList);
        
        ~UnixSocketServer();
        
        /**
         * @brief 启动服务器
         */
        void startServer();
        
        /**
         * @brief 停止服务器
         */
        void stopServer();
        
        /**
         * @brief 获取当前连接的客户端数量
         * @return 连接数
         */
        int getConnectedClientsCount() const;
        
        // 删除拷贝构造函数和赋值运算符
        UnixSocketServer(const UnixSocketServer&) = delete;
        UnixSocketServer& operator=(const UnixSocketServer&) = delete;
        
    private:
        /**
         * @brief 客户端连接上下文
         */
        struct ClientContext {
            int socketFd;
            std::string address;
            std::atomic<bool> stopFlag{false};
            std::unique_ptr<std::thread> receiveThread;
        };
        
        /**
         * @brief 处理单个客户端连接的线程函数
         * @param context 客户端上下文
         * @param cycle_time_ms 数据发送周期时间（毫秒）
         */
        void clientHandler(std::shared_ptr<ClientContext> context,int cycle_time_ms = 500);
        
        /**
         * @brief 从客户端接收数据的线程函数
         * @param context 客户端上下文
         */
        void receiveDataFromQt(std::shared_ptr<ClientContext> context);
        
        /**
         * @brief 发送数据到指定客户端
         * @param context 客户端上下文
         * @return 是否发送成功
         */
        bool sendDataToClient(std::shared_ptr<ClientContext> context);
        
        /**
         * @brief 清理服务器资源
         */
        void cleanup();
        
    private:
        std::string m_socketPath;
        std::vector<std::shared_ptr<Device>> m_devList; // 设备列表副本（持有所有权）
        int m_serverFd{-1};
        std::atomic<bool> m_stopFlag{false};
        
        // 活动连接管理
        std::vector<std::shared_ptr<ClientContext>> m_activeConnections;
        mutable std::mutex m_connectionsMutex;
        
        // 服务器线程
        std::unique_ptr<std::thread> m_serverThread;
        
        // 控制模块（假设已存在）
        // Control& m_control; // 如果需要在C++中实现control.parse_qt_data
};
#endif // UNIXSOCKETSERVER_H