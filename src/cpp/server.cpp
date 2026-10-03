#include "server.hpp"
#include "log.hpp"
#include <stdexcept>

TcpServer::TcpServer(int port)
    : _count_id(0),
      _port(static_cast<uint16_t>(port)),
      _timeout(0),
      _enable_inactive_realse(false),
      _mainloop(),
      _pool(&_mainloop),
      _acceptor(&_mainloop, "0.0.0.0", _port)
{
    if (port < 0 || port > 65535)
        throw std::invalid_argument("TcpServer port must be between 0 and 65535");

    _acceptor.SetAcceptCallbak([this](int client_fd)
                               { NewConnection(client_fd); });
    INF_LOG("TcpServer 已创建，监听端口=%u", static_cast<unsigned>(_port));
}

void TcpServer::StartServer()
{
    _pool.Create();
    INF_LOG("TcpServer 开始运行主 EventLoop");
    _mainloop.StartEventLoop();
}

void TcpServer::StopServer()
{
    _mainloop.StopEventLoop();
}

uint16_t TcpServer::GetListenPort() const
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(_acceptor.GetListenSocketFd(), reinterpret_cast<sockaddr *>(&addr), &len) != 0)
    {
        ERR_LOG("TcpServer 获取客户端端口号错误");
        return -1;
    }
    return ntohs(addr.sin_port);
}

void TcpServer::SetThreadCount(int count)
{
    _pool.SetThreadCount(count);
    INF_LOG("TcpServer 设置工作线程数量=%d", count);
}

void TcpServer::SetConnectedCallback(const ConnectedCallback &cb)
{
    _connected_callback = cb;
}

void TcpServer::SetMessageCallback(const MessageCallback &cb)
{
    _message_callback = cb;
}

void TcpServer::SetCloseCallback(const CloseCallback &cb)
{
    _close_callback = cb;
}

void TcpServer::SetAnyCallback(const AnyCallback &cb)
{
    _any_callback = cb;
}

void TcpServer::NewConnection(int client_fd)
{
    if (!_mainloop.IsInLoopThread())
    {
        ERR_LOG("TcpServer::NewConnection 必须在主 Loop 线程执行");
        return;
    }

    const uint64_t conn_id = _count_id++;
    EventLoop *loop = _pool.NextLoop();
    if (loop == nullptr)
    {
        ERR_LOG("TcpServer 没有可用的连接 EventLoop，关闭 client_fd=%d", client_fd);
        close(client_fd);
        return;
    }

    // Connection 必须在所属 Loop 线程创建，否则它注册 Channel/Poller 时会跨线程操作。
    loop->QueueInLoop([this, client_fd, conn_id, loop]()
                      {
                          try
                          {
                              auto connection = std::make_shared<Connection>(loop, client_fd, conn_id);
                              {
                                  std::lock_guard<std::mutex> lock(_conns_mutex);
                                  _conns[conn_id] = connection;
                              }

                              connection->SetConnectedCallback(_connected_callback);
                              connection->SetMessageCallback(_message_callback);
                              connection->SetAnyCallback(_any_callback);
                              connection->SetCloseCallback(_close_callback);
                              connection->Established();

                              if (_enable_inactive_realse && _timeout > 0)
                                  connection->SetInactiveClose(true, _timeout);
                              INF_LOG("TcpServer 已将连接 id=%llu 分配到 EventLoop",static_cast<unsigned long long>(conn_id));
                          }
                          catch (const std::exception &error)
                          {
                              ERR_LOG("创建连接失败，id=%llu，error=%s",static_cast<unsigned long long>(conn_id), error.what());
                              close(client_fd);
                          } });
}

void TcpServer::EnableInactiveRelease(int timeout)
{
    _timeout = timeout;
    _enable_inactive_realse = true;
}

void TcpServer::RemoveConnection(const ConnPtr &conn)
{
    _mainloop.RunInLoop([this, conn]
                        {
                            const uint64_t id = conn->GetConnId();
                            {
                                std::lock_guard<std::mutex> lock(_conns_mutex);
                                _conns.erase(id);
                            }
                            INF_LOG("%d 号连接已经被删除",conn->GetConnId()); });
}

void TcpServer::RunAfterInLoop(const TaskFunc &task, int sec)
{
    _mainloop.AddTask(_count_id++, sec, task);
}

void TcpServer::RunAfter(const TaskFunc &task, int sec)
{
    _mainloop.RunInLoop([this, task, sec]
                        { RunAfterInLoop(task, sec); });
}
