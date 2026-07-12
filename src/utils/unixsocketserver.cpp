// UnixSocketServer.cpp
#include "unixsocketserver.h"
#include <cstring>
#include <iostream>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "device.h" 
#include "json.hpp"
#include "qtcontroller.h"
#include "log.h"


UnixSocketServer::UnixSocketServer(const std::string& socketPath,
                                   const std::vector<std::shared_ptr<Device>> devList)
    : m_socketPath(socketPath)
    , m_devList(devList)
    , m_serverFd(-1)
    , m_stopFlag(false)
    , m_serverThread(nullptr) {
    
    // 清理可能已存在的socket文件
    std::remove(socketPath.c_str());
}

UnixSocketServer::~UnixSocketServer() {
    stopServer();
    cleanup();
}

void UnixSocketServer::startServer() {
    if (this->m_serverThread && this->m_serverThread->joinable()) {
        LOG_WARNING_LOC("服务器已在运行");
        return;
    }
    
    this->m_stopFlag = false;
    
    this->m_serverThread = std::make_unique<std::thread>([this]() {
        // 创建Unix Socket
        this->m_serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (this->m_serverFd < 0) {
            LOG_ERROR_LOC("创建socket失败: " + std::string(strerror(errno)));
            return;
        }
        
        // 设置socket选项，允许地址重用
        int opt = 1;
        if (setsockopt(this->m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            LOG_ERROR_LOC("设置socket选项失败");
            close(this->m_serverFd);
            this->m_serverFd = -1;
            return;
        }
        
        // 绑定地址
        struct sockaddr_un serverAddr{};
        // memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sun_family = AF_UNIX;
        // strncpy(serverAddr.sun_path, m_socketPath.c_str(), sizeof(serverAddr.sun_path) - 1);
        std::snprintf(serverAddr.sun_path,
              sizeof(serverAddr.sun_path),
              "%s",
              m_socketPath.c_str());
        
        if (bind(this->m_serverFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            LOG_ERROR_LOC("绑定socket失败: " + std::string(strerror(errno)));
            close(this->m_serverFd);
            this->m_serverFd = -1;
            return;
        }
        
        // 开始监听
        if (listen(this->m_serverFd, 5) < 0) {
            LOG_ERROR_LOC("监听失败: " + std::string(strerror(errno)));
            close(this->m_serverFd);
            this->m_serverFd = -1;
            return;
        }
        
        LOG_INFO_LOC("Unix Socket服务器启动，等待客户端连接...");
        
        // 主接受循环
        while (!this->m_stopFlag) {
            struct sockaddr_un clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            
            // 设置非阻塞或使用select/poll，这里简化为阻塞accept
            int clientFd = accept(this->m_serverFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd < 0) {
                if (errno == EINTR) {
                    // 被信号中断，检查停止标志
                    continue;
                }
                LOG_ERROR_LOC("接受连接失败: " + std::string(strerror(errno)));
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            
            // 获取客户端地址信息
            std::string clientAddress = "QT-Client-" + std::to_string(clientFd);
            LOG_INFO_LOC("新的QT客户端连接: " + clientAddress);
            
            // 创建客户端上下文
            auto context = std::make_shared<ClientContext>();
            context->socketFd = clientFd;
            context->address = clientAddress;
            
            // 添加到活动连接列表
            {
                std::lock_guard<std::mutex> lock(this->m_connectionsMutex);
                this->m_activeConnections.push_back(context);
            }
            
            // 启动客户端处理线程
            std::thread clientThread(&UnixSocketServer::clientHandler, this, context, 500);
            clientThread.detach(); // 分离线程，由系统管理生命周期
        }
    });
}
        
void UnixSocketServer::stopServer() {
    this->m_stopFlag = true;
    
    // 关闭服务器socket以中断accept
    if (this->m_serverFd >= 0) {
        shutdown(this->m_serverFd, SHUT_RDWR);
        close(this->m_serverFd);
        this->m_serverFd = -1;
    }
    
    // 停止所有客户端连接
    {
        std::lock_guard<std::mutex> lock(this->m_connectionsMutex);
        for (const auto& context : this->m_activeConnections) {
            context->stopFlag = true;
            shutdown(context->socketFd, SHUT_RDWR);
        }
    }
    
    // 等待服务器线程结束
    if (this->m_serverThread && this->m_serverThread->joinable()) {
        this->m_serverThread->join();
        this->m_serverThread.reset();
    }
    
    // 等待一小段时间让所有客户端线程结束
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    LOG_INFO_LOC("Unix Socket服务器已停止");
}

int UnixSocketServer::getConnectedClientsCount() const {
    std::lock_guard<std::mutex> lock(m_connectionsMutex);
    return static_cast<int>(m_activeConnections.size());
}

void UnixSocketServer::clientHandler(std::shared_ptr<ClientContext> context,int cycle_time_ms) {
    try {
        // 启动接收线程
        context->receiveThread = std::make_unique<std::thread>(
            &UnixSocketServer::receiveDataFromQt, this, context);
        
        LOG_INFO_LOC("QT客户端 " + context->address + " 连接成功，启动数据发送");
        
        // 主发送循环
        while (!context->stopFlag) {
            try {
                // 控制发送频率
                std::this_thread::sleep_for(std::chrono::milliseconds(cycle_time_ms));
                
                // 发送数据
                if (!sendDataToClient(context)) {
                    LOG_WARNING_LOC("向QT客户端发送数据失败: " + context->address);
                    break;
                }
                
            } catch (const std::exception& e) {
                LOG_ERROR_LOC("发送数据时发生异常: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } catch (...) {
                LOG_ERROR_LOC("发送数据时发生未知异常");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("客户端处理线程异常: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR_LOC("客户端处理线程未知异常");
    }
    
    // 清理连接
    {
        std::lock_guard<std::mutex> lock(this->m_connectionsMutex);
        auto it = std::find(this->m_activeConnections.begin(), 
                           this->m_activeConnections.end(), 
                           context);
        if (it != this->m_activeConnections.end()) {
            this->m_activeConnections.erase(it);
        }
    }
    
    // 设置停止标志
    context->stopFlag = true;
    
    // 等待接收线程结束
    if (context->receiveThread && context->receiveThread->joinable()) {
        context->receiveThread->join();
    }
    
    // 关闭socket
    if (context->socketFd >= 0) {
        close(context->socketFd);
        context->socketFd = -1;
    }
    
    LOG_INFO_LOC("QT客户端 " + context->address + " 连接已关闭");
}

bool UnixSocketServer::sendDataToClient(std::shared_ptr<ClientContext> context) {
    if (context->socketFd < 0 || context->stopFlag) {
        return false;
    }
    
    try {
        // 发送所有设备数据
        for (const auto& dev : this->m_devList) {
            if (!dev) {
                continue;
            }
            
            // 使用线程安全的方法获取 data_to_qt 的副本
            json jsonData = dev->get_data_to_qt_safe();
            
            // 如果数据为空，跳过
            if (jsonData.empty()) {
                continue;
            }
            
            // 序列化JSON
            std::string jsonStr = jsonData.dump();
            
            // 构造消息：4字节长度头（大端序） + JSON数据
            uint32_t dataLength = static_cast<uint32_t>(jsonStr.size());
            uint32_t netLength = htonl(dataLength); // 转换为网络字节序（大端）
            
            // 先发送长度头
            ssize_t sent = send(context->socketFd, &netLength, sizeof(netLength), 0);
            if (sent != sizeof(netLength)) {
                LOG_WARNING_LOC("发送长度头失败");
                return false;
            }
            
            // 发送JSON数据
            sent = send(context->socketFd, jsonStr.data(), jsonStr.size(), 0);
            if (sent != static_cast<ssize_t>(jsonStr.size())) {
                LOG_WARNING_LOC("发送JSON数据不完整");
                return false;
            }
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("发送数据序列化失败: " + std::string(e.what()));
        return false;
    } catch (...) {
        LOG_ERROR_LOC("发送数据未知错误");
        return false;
    }
}

void UnixSocketServer::receiveDataFromQt(std::shared_ptr<ClientContext> context) {
    std::string buffer;
    char recvBuffer[1024];
    
    try {
        while (!context->stopFlag) {
            // 设置接收超时
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            setsockopt(context->socketFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            // 接收数据
            ssize_t bytesRead = recv(context->socketFd, recvBuffer, sizeof(recvBuffer) - 1, 0);
            
            if (bytesRead > 0) {
                recvBuffer[bytesRead] = '\0';
                buffer += recvBuffer;
                
                // 处理完整JSON行（以换行符分隔）
                size_t newlinePos;
                while ((newlinePos = buffer.find('\n')) != std::string::npos) {
                    std::string line = buffer.substr(0, newlinePos);
                    buffer = buffer.substr(newlinePos + 1);
                    
                    if (!line.empty()) {
                        try {
                            // 解析JSON
                            nlohmann::json qtData = nlohmann::json::parse(line);
                            LOG_INFO_LOC("从 " + context->address + " 收到数据: " + line);
                            
                            // 处理接收到的数据
                            QtController::getInstance()->parse_qt_data(qtData);
                            
                        } catch (const nlohmann::json::parse_error& e) {
                            LOG_WARNING_LOC("从 " + context->address + " 收到无效JSON数据: " + line);
                        }
                    }
                }
                
            } else if (bytesRead == 0) {
                // 连接关闭
                LOG_INFO_LOC(context->address + " 连接已关闭");
                break;
            } else {
                // 错误或超时
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    // 超时，继续循环检查停止标志
                    continue;
                } else {
                    // 其他错误
                    LOG_INFO_LOC(context->address + " 连接断开");
                    break;
                }
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR_LOC("接收 " + context->address + " 数据时发生异常: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR_LOC("接收 " + context->address + " 数据时发生未知异常");
    }
    
    // 设置停止标志
    context->stopFlag = true;
    LOG_INFO_LOC(context->address + " 数据接收线程结束");
}

void UnixSocketServer::cleanup() {
    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(m_connectionsMutex);
        for (const auto& context : m_activeConnections) {
            context->stopFlag = true;
            if (context->socketFd >= 0) {
                close(context->socketFd);
            }
        }
        m_activeConnections.clear();
    }
    
    // 关闭服务器socket
    if (m_serverFd >= 0) {
        close(m_serverFd);
        m_serverFd = -1;
    }
    
    // 删除socket文件
    std::remove(m_socketPath.c_str());
}