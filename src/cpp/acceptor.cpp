#include "acceptor.hpp"
#include "log.hpp"
#include <cassert>
#include <cerrno>
#include <stdexcept>
#include <unistd.h>

Acceptor::Acceptor(EventLoop *loop, const std::string &ip, uint16_t port)
    : _loop(loop)
{
    if (_loop == nullptr)
        throw std::invalid_argument("Acceptor requires an EventLoop");

    // 监听 socket 的生命周期由 Acceptor 管理：创建 -> bind -> listen。
    if (!_socket.CreateServer(ip, port))
        throw std::runtime_error("Acceptor failed to create listening socket");

    // 将监听socker设置成非阻塞，防止主reactor阻塞
    const int listen_fd = _socket.GetSocketFd();
    const int flags = fcntl(listen_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) == -1)
        throw std::runtime_error("failed to make listening socket nonblocking");

    // 这个 Channel 监听的是 listen fd；它与每条 Connection 的 Channel 不同。
    _channel = std::make_shared<Channel>(_loop->GetPoller(), _socket.GetSocketFd());
    _channel->SetReadCallbck(std::bind(&Acceptor::HandleRead, this));
    _channel->EnableRead();
    _channel->Update();

    INF_LOG("Acceptor 开始监听：fd=%d, ip=%s, port=%u", _socket.GetSocketFd(), ip.c_str(), static_cast<unsigned>(port));
}

Acceptor::~Acceptor()
{
    // 先从 Poller 移除监听，再由 Socket 析构函数关闭 listen fd。
    if (_channel)
    {
        _channel->DisableAll();
        _channel->Remove();
        _channel.reset();
    }
    INF_LOG("Acceptor 已停止监听，fd=%d", _socket.GetSocketFd());
}

void Acceptor::SetAcceptCallbak(const AcceptCallback &cb)
{
    _accpet_callback = cb;
    INF_LOG("Acceptor 已%s新连接回调", cb ? "设置" : "清除");
}

int Acceptor::GetListenSocketFd() const
{
    return _socket.GetSocketFd();
}

void Acceptor::HandleRead()
{
    assert(_loop->IsInLoopThread());

    // listen fd 可读表示至少有一个客户端连接已经完成握手，等待 accept。
    const int client_fd = _socket.Accept();
    if (client_fd < 0)
    {
        // accept 失败时保留监听；后续新连接仍能再次触发可读事件。
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            ERR_LOG("Acceptor 接收新连接失败，listen_fd=%d, errno=%d", _socket.GetSocketFd(), errno);
        return;
    }

    INF_LOG("Acceptor 接收到新连接：listen_fd=%d, client_fd=%d", _socket.GetSocketFd(), client_fd);

    if (_accpet_callback)
    {
        _accpet_callback(client_fd);
        return;
    }

    // 没有上层接管 client_fd 时必须关闭，避免文件描述符泄漏。
    ERR_LOG("Acceptor 未设置新连接回调，关闭 client_fd=%d", client_fd);
    close(client_fd);
}
