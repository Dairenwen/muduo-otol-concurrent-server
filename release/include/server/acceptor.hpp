#pragma once
#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include "socket.hpp"
#include "eventloop.hpp"
#include "channel.hpp"

class Acceptor
{
    using AcceptCallback = std::function<void(int)>;

private:
    Socket _socket;                    // 监听 socket，只负责接收新连接
    EventLoop *_loop;                  // 监听 socket 所属的 EventLoop
    std::shared_ptr<Channel> _channel; // 监听 socket 的 Channel，由 Poller 共同持有
    AcceptCallback _accpet_callback;

    // 监听 socket 可读时调用：accept 一个客户端 fd，再交给上层回调。
    void HandleRead();

public:
    // 创建、绑定并监听 ip:port，同时把监听 socket 的读事件注册到 loop 的 Poller。
    Acceptor(EventLoop *loop, const std::string &ip, uint16_t port);
    ~Acceptor();

    Acceptor(const Acceptor &) = delete;
    Acceptor &operator=(const Acceptor &) = delete;

    // 上层通常在此回调中用 client_fd 创建 Connection
    void SetAcceptCallbak(const AcceptCallback &cb);

    // 读取监听 fd；测试可用 getsockname 取得端口 0 对应的实际端口。
    int GetListenSocketFd() const;
};
