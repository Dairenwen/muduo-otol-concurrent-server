#include "connection.hpp"
#include "log.hpp"
#include <cassert>
#include <cerrno>
#include <stdexcept>

Connection::Connection(EventLoop *loop, int fd, uint64_t conn_id)
    : _conn_id(conn_id), _socket(fd), _statu(CONNECTING),
      _loop(loop), _enable_inactive_close(false)
{
    if (loop == nullptr || fd < 0)
        throw std::invalid_argument("Connection requires a loop and a valid fd");

    _channel = std::make_shared<Channel>(loop->GetPoller(), fd);
    _socket.SetNonBlocking(true); // 防止某条连接的收发阻塞整个事件循环
}

void Connection::EstablishedInLoop()
{
    assert(_loop->IsInLoopThread());
    if (_statu != CONNECTING)
        return;

    _statu = CONNECTED;
    _channel->EnableRead();
    std::weak_ptr<Connection> weak = shared_from_this();
    _channel->SetReadCallbck([weak]
                             {
        if (auto conn = weak.lock()) conn->HandleRead(); });
    _channel->SetWriteCallbck([weak]
                              {
        if (auto conn = weak.lock()) conn->HandleWrite(); });
    _channel->SetCloseCallbck([weak]
                              {
        if (auto conn = weak.lock()) conn->HandleClose(); });
    _channel->SetErrorCallbck([weak]
                              {
        if (auto conn = weak.lock()) conn->HandleClose(); });
    _channel->SetEventCallbck([weak]
                              {
        if (auto conn = weak.lock()) conn->HandleAny(); });
    _channel->Update(); // 监听可读事件后添加到epoll中
    if (_connected_callback)
        _connected_callback(shared_from_this());
}

void Connection::HandleRead()
{
    assert(_loop->IsInLoopThread()); // 必须在loop线程中执行
    if (_statu != CONNECTED)
    {
        ERR_LOG("连接还未建立，无法读取数据");
        return;
    }
    char data[65536];
    ssize_t ret = _socket.Recv(data, sizeof(data));
    if (ret == 0)
    {
        Shutdown(); // 对端正常关闭
        return;
    }
    if (ret < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return; // 暂时无法读取
        Shutdown(); // 真正的读取错误
        return;
    }

    _In_buffer.WriteAndPush(data, ret);
    if (_In_buffer.ReadAbleSize() > 0 && _message_callback)
        _message_callback(shared_from_this(), _In_buffer);
}

void Connection::SendInLoop(const std::string &msg) // 只负责写入输出缓冲区，发送数据由handlewrite负责
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED || _statu == CONNECTING || msg.empty())
        return;

    _Out_buffer.WriteStringAndPush(msg);
    _channel->EnableWrite(); // 开启写事件监控
    _channel->Update();
}

void Connection::HandleWrite()
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED || _statu == CONNECTING)
    {
        ERR_LOG("状态不对，处理写回调失败，statu: %d", _statu);
        return;
    }

    if (_Out_buffer.ReadAbleSize() > 0)
    {
        ssize_t ret = _socket.Send(_Out_buffer.GetReaderPtr(), _Out_buffer.ReadAbleSize());
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                return; // 内核发送缓冲区满了，保留数据等待下一次可写事件
            ERR_LOG("连接发送失败，fd=%d, errno=%d", _socket.GetSocketFd(), errno);
            if (_In_buffer.ReadAbleSize() > 0 && _message_callback)
                _message_callback(shared_from_this(), _In_buffer); // 通知上层协议处理数据
            return Release();                                      // 真正释放连接
        }
        if (ret == 0)
            return;
        _Out_buffer.MoveReaderPtr(ret);
    }

    if (_Out_buffer.ReadAbleSize() == 0)
    {
        _channel->DisableWrite(); // 没有待发数据关闭监听
        _channel->Update();
        if (_statu == DISCONNECTING)
            Release();
    }
}

void Connection::ShutdownInLoop()
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED || _statu == DISCONNECTING)
        return;

    _statu = DISCONNECTING;
    _channel->DisableRead();
    _channel->Update();
    if (_In_buffer.ReadAbleSize() > 0 && _message_callback) // 输入缓冲区还有数据
        _message_callback(shared_from_this(), _In_buffer);
    if (_Out_buffer.ReadAbleSize() == 0)
        Release();
    else // 输出缓冲区还有数据
    {
        _channel->EnableWrite(); // 继续尝试向fd发送数据
        _channel->Update();
    }
}

void Connection::HandleClose()
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED)
        return;

    if (_In_buffer.ReadAbleSize() > 0 && _message_callback)
        _message_callback(shared_from_this(), _In_buffer); // 通知上层协议处理数据

    Release();
}

void Connection::Release() // 真正释放连接
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED)
        return; // 同一轮可能同时有读、错误、挂断事件，只关闭一次

    _statu = DISCONNECTED;
    _loop->CancelTask(_conn_id); // 及时取消任务，防止使用空指针
    _channel->DisableAll();
    _channel->Remove(); // 先移除 epoll 监控，再关闭 fd
    _socket.Close();
    auto self = shared_from_this();

    try
    {
        if (_close_callback)
            _close_callback(self);
    }
    catch (const std::exception &e)
    {
        ERR_LOG("close callback failed: %s", e.what());
    }
    catch (...)
    {
        ERR_LOG("close callback failed with unknown exception");
    }
    // 不仅要调用用户设置的回调函数还需要调用服务器回调来释放连接；
    if (_server_close_callback)
        _server_close_callback(self);
}

void Connection::HandleAny()
{
    if (_statu == DISCONNECTED)
        return;

    if (_enable_inactive_close)
        _loop->RefreshTask(_conn_id); // 在timerwheel中刷新事件

    if (_any_callback)
        _any_callback(shared_from_this());
}

void Connection::SetConnectedCallback(const ConnectedCallback &cb)
{
    _connected_callback = cb;
}

void Connection::SetMessageCallback(const MessageCallback &cb)
{
    _message_callback = cb;
}

void Connection::SetCloseCallback(const CloseCallback &cb)
{
    _close_callback = cb;
}

void Connection::SetAnyCallback(const AnyCallback &cb)
{
    _any_callback = cb;
}

void Connection::SetServerCloseCallback(const CloseCallback &cb)
{
    _server_close_callback = cb;
}

void Connection::SetInactiveClose(bool enable, uint64_t sec)
{
    if (enable && (sec == 0 || sec >= 60))
    {
        ERR_LOG("inactive timeout must be between 1 and 59 seconds");
        return;
    }

    auto self = shared_from_this();
    _loop->RunInLoop([self, enable, sec]
                     { self->SetInactiveCloseInLoop(enable, sec); });
}

void Connection::SetInactiveCloseInLoop(bool enable, uint64_t sec)
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED)
        return;

    if (_enable_inactive_close)
        _loop->CancelTask(_conn_id);

    _enable_inactive_close = enable;
    if (!enable)
        return;

    std::weak_ptr<Connection> weak = shared_from_this();
    _loop->AddTask(_conn_id, sec, [weak]
                   {
        if (auto conn = weak.lock())
            conn->HandleClose(); });
}

void Connection::SwitchProtocol(
    const Any &context,
    const ConnectedCallback &conn,
    const MessageCallback &msg,
    const CloseCallback &closed,
    const AnyCallback &event)
{
    auto self = shared_from_this();
    _loop->RunInLoop([self, context, conn, msg, closed, event]
                     { self->SwitchProtocolInloop(context, conn, msg, closed, event); });
}

void Connection::SwitchProtocolInloop(const Any &context, const ConnectedCallback &conn,
                                      const MessageCallback &msg, const CloseCallback &closed,
                                      const AnyCallback &event)
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED)
        return;

    // 协议升级后，后续读到的数据交给新协议的消息回调处理。
    _context = context;
    _connected_callback = conn;
    _message_callback = msg;
    _close_callback = closed;
    _any_callback = event;
}

// 三个函数可能在其他业务线程调用，不一定在loop中执行
void Connection::Established()
{
    auto self = shared_from_this();
    _loop->RunInLoop([self]
                     { self->EstablishedInLoop(); });
}

void Connection::Send(const std::string &msg)
{
    if (msg.empty())
        return;

    auto self = shared_from_this();
    _loop->RunInLoop([self, msg]
                     { self->SendInLoop(msg); });
}

void Connection::Shutdown()
{
    auto self = shared_from_this();
    _loop->RunInLoop([self]
                     { self->ShutdownInLoop(); });
}

Connection::~Connection()
{
    // 正常关闭已在 Release() 中清理；这里处理外部直接释放连接的情况。
    if (_statu != DISCONNECTED)
    {
        _loop->CancelTask(_conn_id);
        _channel->DisableAll();
        _channel->Remove();
    }
    // Socket 的析构函数关闭 fd
}

int Connection::GetSocketfd() const { return _socket.GetSocketFd(); }
uint64_t Connection::GetConnId() const { return _conn_id; }
ConnStatu Connection::GetConnStatu() const { return _statu; }
Buffer &Connection::GetInBuffer() { return _In_buffer; }
Buffer &Connection::GetOutBuffer() { return _Out_buffer; }
Any &Connection::GetContext() { return _context; }
