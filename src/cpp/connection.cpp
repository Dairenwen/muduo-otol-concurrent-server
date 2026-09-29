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

void Connection::Established()
{
    if (_statu != CONNECTING)
        return;

    // Channel 可能被 Poller 持有。弱引用既避免循环引用，也避免调用已销毁的连接。
    std::weak_ptr<Connection> weak = shared_from_this();
    _channel->SetReadCallbck([weak]()
                             {
        auto conn = weak.lock();
        if (conn) conn->HandleRead(); });
    _channel->SetWriteCallbck([weak]()
                              {
        auto conn = weak.lock();
        if (conn) conn->HandleWrite(); });
    _channel->SetCloseCallbck([weak]()
                              {
        auto conn = weak.lock();
        if (conn) conn->HandleClose(); });
    _channel->SetErrorCallbck([weak]()
                              {
        auto conn = weak.lock();
        if (conn) conn->HandleClose(); });
    _channel->SetEventCallbck([weak]()
                              {
        auto conn = weak.lock();
        if (conn) conn->HandleAny(); });

    _statu = CONNECTED;
    _channel->EnableRead();
    _channel->Update(); // 本项目 EnableRead 只改事件标记，还需要显式 Update
    auto cb = _connected_callback;
    if (cb)
        cb(shared_from_this());
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
    ssize_t ret = _socket.Recv(data, 65536);
    if (ret < 0)
    {
        // 读取发生错误时不能立即 close，先让 Shutdown 处理待发送的数据。
        Shutdown();
        return;
    }

    if (ret > 0)
        _In_buffer.WriteAndPush(data, ret); // 写入缓冲区，并移动写指针

    if (_In_buffer.ReadAbleSize() > 0 && _message_callback)
        _message_callback(shared_from_this(), _In_buffer); // 通知上层协议处理数据
}

void Connection::Send(const std::string &msg)
{
    if (_statu != CONNECTED || msg.empty())
        return;

    // send() 可能只发送一部分数据，所以先写入输出缓冲区。
    // 剩余数据由 HandleWrite() 在下一次 EPOLLOUT 时继续发送。
    _Out_buffer.WriteStringAndPush(msg);
    _channel->EnableWrite();
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
            if (errno == EAGAIN || errno == EWOULDBLOCK)
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

void Connection::Shutdown()
{
    if (_statu == DISCONNECTED || _statu == DISCONNECTING)
        return;

    _statu = DISCONNECTING;
    _channel->DisableRead();
    if (_Out_buffer.ReadAbleSize() == 0)
        Release();
    else
    {
        _channel->EnableWrite(); // 先发完缓冲区，不能直接 close 丢掉回复
        _channel->Update();
    }
}

void Connection::HandleClose()
{
    assert(_loop->IsInLoopThread());
    if (_statu == DISCONNECTED)
        return;

    Release();
}

void Connection::Release()
{
    if (_statu == DISCONNECTED)
        return; // 同一轮可能同时有读、错误、挂断事件，只关闭一次

    auto self = shared_from_this(); // 关闭回调可能删除服务器保存的最后一个引用
    _statu = DISCONNECTED;
    if (_enable_inactive_close)
    {
        _loop->CancelTask(_conn_id);
        _enable_inactive_close = false;
    }
    _channel->DisableAll();
    _channel->Remove(); // 先移除 epoll 监控，再关闭 fd
    _socket.Close();
    auto cb = _close_callback;
    if (cb)
        cb(self);
}

void Connection::HandleAny()
{
    if (_statu == DISCONNECTED)
        return;
    if (_enable_inactive_close)
        _loop->RefreshTask(_conn_id);
    auto cb = _any_callback;
    if (cb)
        cb(shared_from_this());
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

void Connection::SetInactiveClose(bool enable, uint64_t sec)
{
    // 当前时间轮只有 60 个槽，超过范围会取模，导致提前超时。
    if (enable && (sec == 0 || sec >= 60))
        throw std::invalid_argument("inactive timeout must be between 1 and 59 seconds");
    if (_statu == DISCONNECTED)
        return;
    if (_enable_inactive_close)
        _loop->CancelTask(_conn_id);
    _enable_inactive_close = enable;
    if (!enable)
        return;

    std::weak_ptr<Connection> weak = shared_from_this();
    _loop->AddTask(_conn_id, sec, [weak]()
                   {
        auto conn = weak.lock();
        // TimerTask 到期时执行此回调；连接还存在才关闭。
        if (conn) conn->HandleClose(); });
}

void Connection::SwitchProtocol(const Any &context, const ConnectedCallback &conn,
                                const MessageCallback &msg, const CloseCallback &closed,
                                const AnyCallback &event)
{
    if (_statu == DISCONNECTED)
        return;

    // 协议升级后，后续读到的数据交给新协议的消息回调处理。
    _context = context;
    _connected_callback = conn;
    _message_callback = msg;
    _close_callback = closed;
    _any_callback = event;
}

Connection::~Connection()
{
    // 正常关闭已在 Release() 中清理；这里处理外部直接释放连接的情况。
    // 最后一个 shared_ptr 应在 Loop 线程中释放，且 Loop 仍然存活。
    if (_statu != DISCONNECTED)
    {
        if (_enable_inactive_close)
            _loop->CancelTask(_conn_id);
        _channel->DisableAll();
        _channel->Remove();
    }
    // Socket 的析构函数负责关闭 fd。
}

int Connection::GetSocketfd() const { return _socket.GetSocketFd(); }
uint64_t Connection::GetConnId() const { return _conn_id; }
ConnStatu Connection::GetConnStatu() const { return _statu; }
Buffer &Connection::GetInBuffer() { return _In_buffer; }
Buffer &Connection::GetOutBuffer() { return _Out_buffer; }
Any &Connection::GetContext() { return _context; }
