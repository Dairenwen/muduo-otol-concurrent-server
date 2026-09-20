#include "socket.hpp"
#include "log.hpp"

Socket::Socket(int fd) : _socket_fd(fd) {}

const int Socket::GetSocketFd() const
{
    return _socket_fd;
}
bool Socket::Create()
{
    if (_socket_fd >= 0)
    {
        Close();
    }
    // socket(): 创建 TCP 套接字，返回 fd；AF_INET=IPv4，SOCK_STREAM=字节流
    if ((_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        ERR_LOG("创建套接字失败，errno = %d", errno);
        return false;
    }
    else
    {
        INF_LOG("创建套接字成功，fd = %d", _socket_fd);
        return true;
    }
}

bool Socket::Bind(uint16_t port, const std::string &ip)
{
    if (_socket_fd < 0)
    {
        FATAL_LOG("套接字未创建，无法绑定，fd = %d", _socket_fd);
        return false;
    }

    // sockaddr_in: IPv4 地址结构，包含 family + port + ip
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;   // IPv4 协议族
    addr.sin_port = htons(port); // 端口转成网络字节序

    if (ip.empty() || ip == "0.0.0.0")
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY); // 绑定到所有网卡
    }
    else
    {
        addr.sin_addr.s_addr = inet_addr(ip.c_str()); // 点分十进制 IP 转成 32-bit 网络地址
        if (addr.sin_addr.s_addr == INADDR_NONE)      // 无效的 IP 地址
        {
            ERR_LOG("无效的 IP 地址: %s", ip.c_str());
            return false;
        }
    }

    // bind(): 把 fd 绑定到指定 IP:PORT；用于服务端监听地址
    if (bind(_socket_fd, (struct sockaddr *)(&addr), sizeof(addr)) == 0)
    {
        INF_LOG("绑定成功，fd = %d, ip = %s, port = %d", _socket_fd, ip.c_str(), port);
        return true;
    }
    else
    {
        ERR_LOG("绑定失败，errno = %d", errno);
        return false;
    }
}

bool Socket::Listen(int backlog)
{
    if (_socket_fd < 0)
    {
        FATAL_LOG("套接字未创建，无法监听，fd = %d", _socket_fd);
        return false;
    }

    // listen(): 进入监听状态，允许 accept() 接收连接
    return listen(_socket_fd, backlog) == 0;
}

int Socket::Accept()
{
    if (_socket_fd < 0)
    {
        ERR_LOG("套接字未创建，无法接受连接，fd = %d", _socket_fd);
        return -1;
    }

    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    // accept(): 接受客户端连接，返回新的连接 fd
    if (accept(_socket_fd, (struct sockaddr *)(&client_addr), &client_len) < 0)
    {
        ERR_LOG("接受连接失败，errno = %d", errno);
        return -1;
    }
    else
    {
        INF_LOG("接受连接成功，fd = %d", client_addr.sin_addr.s_addr);
        return client_addr.sin_addr.s_addr; // 返回客户端 IP 地址的网络字节序表示
    }
}

ssize_t Socket::Recv(void *buf, size_t len, int flags)
{
    if (_socket_fd < 0 || buf == nullptr)
    {
        ERR_LOG("套接字未创建或缓冲区为空，无法接收数据，fd = %d", _socket_fd);
        return -1;
    }

    // recv(): 从 socket 读数据，返回读取到的字节数
    ssize_t ret = recv(_socket_fd, buf, len, flags);
    if (ret < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            // 非阻塞模式下，没有数据可读,或者被信号中断，返回 0 表示没有读取到数据
            WARN_LOG("接收数据失败，errno = %d, 可能是非阻塞模式下没有数据可读或被信号中断", errno);
            return 0;
        }
        else
        {
            ERR_LOG("接收数据失败，errno = %d", errno);
            return -1;
        }
    }
    else
    {
        INF_LOG("接收数据成功，fd = %d, 接收到 %zd 字节", _socket_fd, ret);
        return ret;
    }
}

ssize_t Socket::Send(const void *buf, size_t len, int flags)
{
    if (_socket_fd < 0 || buf == nullptr)
    {
        ERR_LOG("套接字未创建或缓冲区为空，无法发送数据，fd = %d", _socket_fd);
        return -1;
    }

    // send(): 向 socket 写数据，返回发送的字节数
    ssize_t ret = send(_socket_fd, buf, len, flags);
    if (ret < 0)
    {
        ERR_LOG("发送数据失败，errno = %d", errno);
        return -1;
    }
    else
    {
        INF_LOG("发送数据成功，fd = %d, 发送了 %zd 字节", _socket_fd, ret);
        return ret;
    }
}

bool Socket::Connect(const std::string &ip, uint16_t port)
{
    if (ip.empty())
    {
        WARN_LOG("IP 地址为空，无法连接");
        return false;
    }

    if (_socket_fd < 0 && !Create())
    {
        ERR_LOG("套接字为空且创建失败，无法连接，fd = %d", _socket_fd);
        return false;
    }

    // sockaddr_in: 客户端连接地址，family/port/ip 都要填好
    sockaddr_in addr{};
    std::memset(&addr, 0, sizeof(addr));          // 先清零再设置
    addr.sin_family = AF_INET;                    // IPv4
    addr.sin_port = htons(port);                  // 端口统一用网络字节序
    addr.sin_addr.s_addr = inet_addr(ip.c_str()); // 点分十进制 IP 转成 32-bit 网络地址

    if (addr.sin_addr.s_addr == INADDR_NONE)
    {
        ERR_LOG("无效的 IP 地址: %s", ip.c_str());
        return false;
    }

    // connect(): 客户端主动连接目标 IP:PORT
    if (connect(_socket_fd, (struct sockaddr *)(&addr), sizeof(addr)) == 0)
    {
        INF_LOG("连接成功，fd = %d, ip = %s, port = %d", _socket_fd, ip.c_str(), port);
        return true;
    }
    else
    {
        ERR_LOG("连接失败，errno = %d", errno);
        return false;
    }
}

bool Socket::CreateServer(const std::string &ip, uint16_t port)
{
    if (!Create())
    {
        ERR_LOG("创建套接字失败，无法创建服务器，fd = %d", _socket_fd);
        return false;
    }

    SetReuseAddr(true); // 设置地址复用，timewait 状态下也能快速重启服务器
    INF_LOG("设置地址复用成功，fd = %d", _socket_fd);

    if (!Bind(port, ip))
    {
        ERR_LOG("绑定端口失败，无法创建服务器，fd = %d", _socket_fd);
        Close();
        return false;
    }
    INF_LOG("绑定端口成功，fd = %d", _socket_fd);

    if (!Listen())
    {
        ERR_LOG("监听失败，无法创建服务器，fd = %d", _socket_fd);
        Close();
        return false;
    }
    INF_LOG("监听成功，fd = %d", _socket_fd);

    return true;
}

bool Socket::CreateClient(const std::string &ip, uint16_t port)
{
    if (!Create())
    {
        ERR_LOG("创建套接字失败，无法创建客户端，fd = %d", _socket_fd);
        return false;
    }
    INF_LOG("创建套接字成功，fd = %d", _socket_fd);
    if (!Connect(ip, port))
    {
        ERR_LOG("连接服务器失败，无法创建客户端，fd = %d", _socket_fd);
        Close();
        return false;
    }
    INF_LOG("连接服务器成功，fd = %d", _socket_fd);
    return true;
}

void Socket::SetNonBlocking(bool non_blocking)
{
    if (_socket_fd < 0)
    {
        ERR_LOG("套接字未创建，无法设置非阻塞模式，fd = %d", _socket_fd);
        return;
    }

    // fcntl(): 读/写 fd 状态标志；这里用来设 O_NONBLOCK
    int flags = fcntl(_socket_fd, F_GETFL, 0);
    // F_GETFL: 获取文件状态标志，0: 默认参数
    if (flags < 0)
    {
        ERR_LOG("获取文件状态标志失败，fd = %d", _socket_fd);
        return;
    }
    if (non_blocking)
    {
        flags |= O_NONBLOCK; // 设置非阻塞模式
    }
    else
    {
        flags &= ~O_NONBLOCK; // 清除非阻塞模式
    }

    fcntl(_socket_fd, F_SETFL, flags);
    INF_LOG("设置非阻塞模式成功，fd = %d, non_blocking = %d", _socket_fd, non_blocking);
}

void Socket::SetReuseAddr(bool reuse)
{
    if (_socket_fd < 0)
    {
        ERR_LOG("套接字未创建，无法设置地址复用，fd = %d", _socket_fd);
        return;
    }

    // setsockopt(): 设置套接字选项，如端口复用
    int optval = reuse ? 1 : 0;
    setsockopt(_socket_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    // SOL_SOCKET: 套接字层选项，SO_REUSEADDR: 允许地址复用，optval: 选项值，sizeof(optval): 选项值大小
    INF_LOG("设置地址复用成功，fd = %d, reuse = %d", _socket_fd, reuse);
}

void Socket::Close()
{
    if (_socket_fd >= 0)
    {
        // close(): 关闭 fd，释放文件描述符
        close(_socket_fd);
        _socket_fd = -1;
    }
}
