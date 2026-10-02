#pragma once
#include "eventloop.hpp"
#include "acceptor.hpp"
#include "LoopThreadPool.hpp"
#include "connection.hpp"
#include <unordered_map>
#include <mutex>

using Functor = std::function<void()>;

class TcpServer
{
private:
    uint64_t _count_id;                           // id
    uint16_t _port;                               // 监听端口号
    uint64_t _timeout;                            // 设置超时事件
    bool _enable_inactive_realse;                 // 是否启动非活跃连接释放
    EventLoop _mainloop;                          // 主reactor
    LoopThreadPool _pool;                         // 线程池
    Acceptor _acceptor;                           // 监听套接字，必须在主 Loop 之后构造
    std::unordered_map<uint64_t, ConnPtr> _conns; // 保存管理的shareptr连接对象
    std::mutex _conns_mutex;                      // 保护连接表的跨线程访问

    using ConnectedCallback = std::function<void(const ConnPtr &)>;
    using MessageCallback = std::function<void(const ConnPtr &, Buffer &)>;
    using CloseCallback = std::function<void(const ConnPtr &)>;
    using AnyCallback = std::function<void(const ConnPtr &)>;

    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    CloseCallback _close_callback;
    AnyCallback _any_callback;

    void NewConnection(int client_fd);          // 给监听到的 fd 创建一个新连接
    void RemoveConnection(const ConnPtr &conn); // 移除指定连接
    void RunAfterInLoop(const TaskFunc &task, int sec);

public:
    TcpServer(int port);
    void StartServer();
    void SetThreadCount(int count);
    void EnableInactiveRelease(int timeout);
    void SetConnectedCallback(const ConnectedCallback &cb);
    void SetMessageCallback(const MessageCallback &cb);
    void SetCloseCallback(const CloseCallback &cb);
    void SetAnyCallback(const AnyCallback &cb);
    void RunAfter(const TaskFunc &task, int sec);
};
