#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <string>

class Socket
{
private:
    int _socket_fd; // 套接字文件描述符

public:
    Socket() : _socket_fd(-1) {}
    Socket(int fd);
    const int GetSocketFd() const;
    ~Socket() { Close(); }

    bool Create();                                            // 创建套接字
    bool Bind(uint16_t port, const std::string &ip);          // 绑定套接字
    bool Listen(int backlog = SOMAXCONN);                     // 监听
    int Accept();                                             // 接受连接
    ssize_t Recv(void *buf, size_t len, int flags = 0);       // 接收数据
    ssize_t Send(const void *buf, size_t len, int flags = 0); // 发送数据
    bool Connect(const std::string &ip, uint16_t port);       // 连接服务器
    bool CreateServer(const std::string &ip, uint16_t port);  // 连接服务器
    bool CreateClient(const std::string &ip, uint16_t port);  // 连接客户端
    void SetNonBlocking(bool non_blocking);                   // 设置非阻塞模式
    void SetReuseAddr(bool reuse);                            // 设置地址复用
    void Close();                                             // 关闭套接字
};
