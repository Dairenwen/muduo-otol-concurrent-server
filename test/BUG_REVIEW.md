# 严重 bug 审查与手动修改方案

本文件是审查结果，下面的底层修复**没有应用到项目代码**。位置按本次检查结束时的文件计算；修改后行号会移动，请同时按函数名定位。这里仅讨论正确性、崩溃、资源泄漏与生命周期，不讨论优化。

## 已修改并验证的第 1 项

`src/hpp/server.hpp:77` 的 EchoServer::Onmessaged 已改为按实际长度回显。原实现返回固定字符串并立即 Shutdown，因此不符合通常的 EchoServer 语义；原来的 `%s` 日志要求零结尾，但 Buffer 不保证零结尾，会越界读，并且不能正确显示二进制。

增加 GetListenPort（使用端口 0）、StopServer（请求主循环停止）及 EchoServer 转发接口，以便测试使用临时端口和结束事件循环。StopServer 目前只停止主循环，不能当作完整的连接清理接口；本次测试在连接全部关闭后调用它。完整关闭方案见问题 3。

`test/test.cpp:1144` 按注释实现了父进程服务器、子进程客户端，管道传递端口和结果，TCP 传递业务数据。覆盖：同一连接多次收发、分段数据、含零字节的二进制、256 KiB 数据、8 客户端并发、半关闭、10 秒空闲关闭、线程回收。所有比较使用运行时检查，不依赖 assert。CMake 增加 45 秒测试超时。

执行环境：已有 Docker 容器 ubuntu2404，Ubuntu 24.04 / GCC 13.3。Debug CTest 通过（10.01 秒）；AddressSanitizer + UndefinedBehaviorSanitizer CTest 通过（10.02 秒）。当前 main 只调用 testtcpserver，不能据此声称其余所有历史测试都通过。附加缺陷探针在 /tmp 编译，不混入项目测试入口。

## 1. [P1] SIGPIPE 可以结束整个服务器（已复现）

位置：`src/cpp/socket.cpp`，Socket::Send 内调用 send 的语句（约第 127 行）；`src/cpp/connection.cpp`，HandleWrite 使用默认 flags。

客户端关闭后，服务端继续写入可能收到 SIGPIPE，默认行为是结束整个进程。send 返回值检查来不及处理默认的致命信号。本次使用 socketpair，关闭接收端后调用当前 Socket::Send，退出码为 141（128 + SIGPIPE）。这验证了封装层的问题；不是声称正常 Echo 测试出现了该故障。

手动替换 Socket::Send 中这一句：

```cpp
ssize_t ret = send(_socket_fd, buf, len, flags | MSG_NOSIGNAL);
```

保留后续错误处理，EPIPE 等真实发送错误仍交给 Connection 关闭连接。项目本来就面向 Linux，MSG_NOSIGNAL 在该环境可用。

## 2. [P1] 已关闭连接永远留在 _conns（静态确认）

位置：`src/cpp/server.cpp:80` 插入连接；第 86 行仅设置用户关闭回调；第 106 行 RemoveConnection 没有任何调用处。

关闭 fd 并不释放 _conns 持有的 shared_ptr。每条短连接的 Connection、输入输出 Buffer、上下文一直保留到服务器析构。反复连接可导致内存不断增长，最终耗尽内存。

在 NewConnection 中，将 `connection->SetCloseCallback(_close_callback);` 替换为：

```cpp
const auto user_close = _close_callback;
connection->SetCloseCallback([this, user_close](const ConnPtr &conn)
{
    // 用户关闭回调的异常不能阻止服务器回收连接。
    try
    {
        if (user_close) user_close(conn);
    }
    catch (const std::exception &e)
    {
        ERR_LOG("close callback failed: %s", e.what());
    }
    catch (...)
    {
        ERR_LOG("close callback failed with unknown exception");
    }
    RemoveConnection(conn);
});
```

同时把 RemoveConnection 中的日志格式改为匹配 uint64_t，避免可变参数类型不匹配：

```cpp
INF_LOG("%llu 号连接已经被删除",
        static_cast<unsigned long long>(conn->GetConnId()));
```

这个回调包装也用于问题 7 的替换代码。SwitchProtocol 会覆盖用户关闭回调；如果需要在 TcpServer 上使用协议升级，必须把“服务器内部连接移除回调”独立保存到 Connection 的成员中，在 Release 中无条件调用，避免升级再覆盖这层包装。当前 EchoServer 没有调用 SwitchProtocol。

如果要覆盖现有 SwitchProtocol 接口，使用下面的独立内部回调方案，替代上面的关闭回调包装。在 `connection.hpp` 的 private 中增加 `CloseCallback _server_close_callback;`，public 中增加：

```cpp
void SetServerCloseCallback(const CloseCallback &cb)
{
    _server_close_callback = cb;
}
```

NewConnection 中设置回调时使用：

```cpp
connection->SetCloseCallback(_close_callback);
connection->SetServerCloseCallback([this](const ConnPtr &conn)
{
    RemoveConnection(conn);
});
```

将 Connection::Release 中最后的 `if (_close_callback) ...` 替换为：

```cpp
auto self = shared_from_this();
try
{
    if (_close_callback) _close_callback(self);
}
catch (const std::exception &e)
{
    ERR_LOG("close callback failed: %s", e.what());
}
catch (...)
{
    ERR_LOG("close callback failed with unknown exception");
}
if (_server_close_callback) _server_close_callback(self);
```

SwitchProtocolInloop 保持原样，不覆盖 `_server_close_callback`。采用这个方案时，问题 7 的替换代码也应使用上述两个 Set 回调语句来代替其包装 lambda；异常清理路径中的 RemoveConnection 可以保留（重复 erase 是安全的），或者在已经注册内部回调时省去该重复调用。


## 3. [P1] 析构时工作线程仍能访问已销毁成员（静态确认）

位置：`src/hpp/server.hpp:18` 到第 32 行的成员排列，以及隐式 ~TcpServer；`src/cpp/connection.cpp` 的 ~Connection。

成员逆序析构：服务器的回调对象、_conns_mutex、_conns 都先于 _pool 销毁，然而 _pool 析构才停止并 join 工作线程。工作线程仍可能运行 NewConnection 的捕获 this 的任务、超时回调或关闭回调；主线程清空活跃 Connection 时，Connection 析构也会直接跨线程操作工作 Poller。后果包括数据竞争、访问已释放内存。仅调整成员顺序不能解决：若先销毁工作 Loop，连接又会保留悬空 _loop。

下面是一套与本项目“主线程构造和运行服务器”的使用方式配套的最小清理方案。**要求 StartServer 已返回，服务器的析构仍在主 Loop 所属线程进行，而且调用方已停止外部投递任务；不得在 StartServer 运行期间直接 delete 服务器。**先停止接入、等候已投递的连接创建任务、在各连接所属线程关闭连接，最后才让线程池析构。

在 `src/hpp/LoopThreadPool.hpp` 的 public 中加入：

```cpp
const std::vector<EventLoop *> &GetLoops() const { return _loops; }
```

在 `src/hpp/connection.hpp` 的 public 中加入：

```cpp
EventLoop *GetLoop() const { return _loop; }
```

在 `src/hpp/server.hpp` 的 public 中声明：

```cpp
~TcpServer();
```

在 `src/cpp/server.cpp` 加入 `<future>`、`<vector>`、`<cassert>`，并添加下面的析构函数。应与问题 2 的关闭回调一起使用：

```cpp
TcpServer::~TcpServer()
{
    assert(_mainloop.IsInLoopThread());
    _acceptor.SetAcceptCallbak({});

    // 主循环已停止，不再产生新的 NewConnection；清掉先前投递的创建任务。
    for (EventLoop *loop : _pool.GetLoops())
    {
        auto barrier = std::make_shared<std::promise<void>>();
        auto ready = barrier->get_future();
        loop->QueueInLoop([barrier] { barrier->set_value(); });
        ready.get();
    }

    std::vector<ConnPtr> connections;
    {
        std::lock_guard<std::mutex> lock(_conns_mutex);
        for (const auto &entry : _conns) connections.push_back(entry.second);
    }
    for (const auto &conn : connections)
    {
        auto barrier = std::make_shared<std::promise<void>>();
        auto ready = barrier->get_future();
        conn->GetLoop()->RunInLoop([conn, barrier]
        {
            // 强制停服时不再处理残留业务数据，防止重新产生待发数据。
            conn->SetMessageCallback({});
            conn->HandleClose();
            barrier->set_value();
        });
        ready.get();
    }
    {
        std::lock_guard<std::mutex> lock(_conns_mutex);
        _conns.clear();
    }
    // 所有 Connection 已 DISCONNECTED；随后 _pool 析构停止并 join 工作线程。
}
```

这些代码依赖工作 Loop 仍在运行；如果业务主动停止了某个子 Loop，就必须先设计线程池级别的停止流程，不能继续向已退出的 Loop 投递并等待。当前正常使用路径由 TcpServer 管理线程池，没有直接停止子 Loop。

## 4. [P1] 监听 socket 为阻塞模式，accept 可能卡住主 Reactor（静态确认）

位置：`src/cpp/acceptor.cpp`，Acceptor 构造函数创建监听 socket 后；`src/cpp/socket.cpp` 的 CreateServer 和 Accept。

只有通信 Connection 设置了非阻塞；监听 fd 没有。Linux 上“epoll 报可读”不保证随后 accept 一定还能得到连接，例如等待接收的连接已经被异步网络错误移除。此时阻塞 accept 会让主 Reactor 无法接受新连接或执行停止请求。

在 Acceptor 构造函数中，CreateServer 成功后、创建 Channel 之前加入以下代码。socket.hpp 已包含 fcntl.h：

```cpp
const int listen_fd = _socket.GetSocketFd();
const int flags = fcntl(listen_fd, F_GETFL, 0);
if (flags == -1 || fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK) == -1)
    throw std::runtime_error("failed to make listening socket nonblocking");
```

HandleRead 已对 EAGAIN/EWOULDBLOCK/EINTR 返回，因此无需为了正确性改成 accept 批量循环。

## 5. [P1] 定时器到期期间刷新自身会解引用空指针（已复现）

位置：`src/cpp/timewheel.cpp:123`，RefreshTaskInLoop；第 85 行 TimerTask 析构执行回调。

任务执行发生在最后一个 shared_ptr 释放期间。这时 _task_map 中仍有该 id，但 weak_ptr::lock 已返回空指针。如果到期回调调用 RefreshTask(id)，第 128 行立即崩溃。ASan/UBSan 实际报告了该空指针错误。

替换 RefreshTaskInLoop：

```cpp
void TimeWheel::RefreshTaskInLoop(uint64_t id)
{
    auto it = _task_map.find(id);
    if (it == _task_map.end()) return;
    auto task = it->second.lock();
    if (!task)
    {
        _task_map.erase(it);
        return;
    }
    _slots[(_tick + task->GetDelayTime()) % _capacity].push_back(task);
}
```

到期后的任务已不能“刷新”，需要回调重新 AddTask 新任务。

另外，为避免用户回调在 `_slots[_tick].clear()` 期间重新向正在清理的槽位插入数据，替换 RunTimerTaskInLoop 为：

```cpp
void TimeWheel::RunTimerTaskInLoop()
{
    _tick = (_tick + 1) % _capacity;
    std::vector<PtrTask> expired;
    expired.swap(_slots[_tick]);
    // 释放局部容器中的任务；回调只会修改时间轮自身的槽，不会重入同一个容器。
    expired.clear();
}
```

## 6. [P2] 长延迟被取模成短延迟，空闲超时无效时静默失效（已复现/静态确认）

位置：`src/cpp/timewheel.cpp:109` 的 AddTaskInLoop、第 114 行槽位计算；`src/cpp/server.cpp:100` 的 EnableInactiveRelease、RunAfter。

时间轮没有记录圈数。本次 `AddTask(id, 61, ...)` 实测约 1000 毫秒后执行，而非 61 秒。0 秒会变成等一圈；负数从 int 转 uint64_t 也会产生意外延迟。EnableInactiveRelease(60) 则在 Connection 层被拒绝，服务器仍显示已开启非活跃释放，实际连接没有超时任务。

这里给出最小修复：明确将现有轮的支持范围限定为 1～59 秒，拒绝不支持的时间。若你希望支持任意长延迟，需要另行实现圈数/绝对到期时间，这里不作扩大设计。

在 TimeWheel::AddTask 最开始、投递任务之前加入：

```cpp
if (timeout == 0 || timeout >= static_cast<uint64_t>(_capacity))
    throw std::invalid_argument("TimeWheel timeout must be between 1 and 59 seconds");
```

替换 EnableInactiveRelease：

```cpp
void TcpServer::EnableInactiveRelease(int timeout)
{
    if (timeout < 1 || timeout >= 60)
        throw std::invalid_argument("inactive timeout must be between 1 and 59 seconds");
    _timeout = static_cast<uint64_t>(timeout);
    _enable_inactive_realse = true;
}
```

在 TcpServer::RunAfter 最开始、投递任务前加入：

```cpp
if (sec < 1 || sec >= 60)
    throw std::invalid_argument("RunAfter delay must be between 1 and 59 seconds");
```

## 7. [P1] 创建连接异常路径二次关闭 fd，且可能留下失效连接（静态确认）

位置：`src/cpp/server.cpp:77` 创建 Connection、第 80 行插入 map、第 96 行 catch 中 close(client_fd)；Connection 构造函数以 `_socket(fd)` 初始化。

如果 Connection 构造在持有 fd 后抛异常，其 Socket 成员已经关闭 fd，catch 再次 close 同一个数字。在多线程进程中，该数字可能已复用成别的连接，导致误关。如果 Established 的用户回调抛异常，map 仍持有 Connection，catch 却直接关闭其底层 fd，状态及 Poller 表也不再一致。

需要统一 fd 的所有权，不能只删掉 catch 中的 close（make_shared 在进入构造函数前分配失败时又会泄漏 fd）。下面代码需要同时使用问题 2 的关闭回调包装。

在 `src/hpp/socket.hpp` 的 public 中加入：

```cpp
int ReleaseFd()
{
    const int fd = _socket_fd;
    _socket_fd = -1;
    return fd;
}
void AdoptFd(int fd)
{
    Close();
    _socket_fd = fd;
}
```

将 `src/cpp/connection.cpp` 的整个构造函数替换为（仅在所有可抛操作完成后接管 fd）：

```cpp
Connection::Connection(EventLoop *loop, int fd, uint64_t conn_id)
    : _conn_id(conn_id), _socket(-1), _statu(CONNECTING),
      _loop(loop), _enable_inactive_close(false)
{
    if (loop == nullptr || fd < 0)
        throw std::invalid_argument("Connection requires a loop and a valid fd");
    _channel = std::make_shared<Channel>(loop->GetPoller(), fd);
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        throw std::runtime_error("failed to make connection nonblocking");
    _socket.AdoptFd(fd);
}
```

在 NewConnection 投递的 lambda 内，替换原来的 try/catch 为以下内容（保留外面的 QueueInLoop）：

```cpp
Socket pending(client_fd); // 构造失败时仍由此对象关闭 fd。
ConnPtr connection;
try
{
    connection = std::make_shared<Connection>(loop, client_fd, conn_id);
    pending.ReleaseFd(); // 构造成功后，仅 Connection 持有 fd。
    {
        std::lock_guard<std::mutex> lock(_conns_mutex);
        _conns[conn_id] = connection;
    }
    connection->SetConnectedCallback(_connected_callback);
    connection->SetMessageCallback(_message_callback);
    connection->SetAnyCallback(_any_callback);
    const auto user_close = _close_callback;
    connection->SetCloseCallback([this, user_close](const ConnPtr &conn)
    {
        try { if (user_close) user_close(conn); }
        catch (const std::exception &e) { ERR_LOG("close callback failed: %s", e.what()); }
        catch (...) { ERR_LOG("close callback failed with unknown exception"); }
        RemoveConnection(conn);
    });
    connection->Established();
    if (_enable_inactive_realse && _timeout > 0)
        connection->SetInactiveClose(true, _timeout);
}
catch (...)
{
    if (connection)
    {
        // 清理异常中的业务回调，避免清理时再次触发同一个异常。
        connection->SetMessageCallback({});
        connection->SetCloseCallback({});
        connection->HandleClose();
        RemoveConnection(connection);
    }
    // 没有 Connection 时 pending 负责关闭；有 Connection 时 HandleClose 关闭。
    ERR_LOG("创建或建立连接失败，id=%llu",
            static_cast<unsigned long long>(conn_id));
}
```

外层 QueueInLoop 的入队本身也可能因内存不足抛异常。如需覆盖该路径，应在调用 QueueInLoop 的外层用 try/catch 包住，并在入队失败时 close(client_fd) 后重新抛出；这时尚未创建任何 Connection。以上方案不尝试让服务器在所有内存耗尽情况下继续提供服务。

## 修复验证边界

第 2 项要求手动修改，因此上述代码均为建议，未应用或作为整体编译测试。已执行的测试验证的是第 1 项 EchoServer 和现有正常路径；附加探针验证问题 1、5、6 确实存在。问题 2、3、4、7 的结论来自具体控制流和所有权分析，不冒充运行复现。手动修改后需要重新运行 Echo 测试，并为连接表回收、带活跃连接停服、断连发送和定时器错误路径补充回归检查。
