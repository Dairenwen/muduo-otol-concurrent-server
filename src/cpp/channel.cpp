#include "channel.hpp"
#include "poller.hpp"
#include "log.hpp"

int Channel::GetSocketFd() const
{
    return _socket_fd;
}

uint32_t Channel::GetEvents() const
{
    return _events;
}

void Channel::SetReadCallbck(const EventCallback &cb)
{
    _read_callback = cb;
}

void Channel::SetWriteCallbck(const EventCallback &cb)
{
    _write_callback = cb;
}

void Channel::SetErrorCallbck(const EventCallback &cb)
{
    _error_callback = cb;
}

void Channel::SetCloseCallbck(const EventCallback &cb)
{
    _close_callback = cb;
}

void Channel::SetEventCallbck(const EventCallback &cb)
{
    _event_callback = cb;
}

bool Channel::ReadAble()
{
    return (_events & (EPOLLIN | EPOLLPRI)) != 0;
}

bool Channel::WriteAble()
{
    return (_events & EPOLLOUT) != 0;
}

void Channel::SetRevents(uint32_t revents)
{
    _revents = revents;
}

void Channel::EnableRead()
{
    _events |= EPOLLIN;
    INF_LOG("Channel 开启读事件，events = %u", _events);
}

void Channel::EnableWrite()
{
    _events |= EPOLLOUT;
    INF_LOG("Channel 开启写事件，events = %u", _events);
}

void Channel::DisableRead()
{
    _events &= ~(EPOLLIN | EPOLLPRI); // 同时取消普通读事件以及紧急读事件
    INF_LOG("Channel 关闭读事件，events = %u", _events);
}

void Channel::DisableWrite()
{
    _events &= ~EPOLLOUT;
    INF_LOG("Channel 关闭写事件，events = %u", _events);
}

void Channel::DisableAll()
{
    _events = 0;
    INF_LOG("Channel 关闭全部事件");
}

void Channel::Update()
{
    if (_poller == nullptr)
    {
        ERR_LOG("Channel 更新事件监控失败：Poller 为空");
        return;
    }

    // Channel 必须通过 std::make_shared<Channel> 创建。
    _poller->UpdateEvent(shared_from_this());
    INF_LOG("Channel 更新事件监控，events = %u", _events);
}

void Channel::Remove()
{
    if (_poller == nullptr)
    {
        ERR_LOG("Channel 移除事件监控失败：Poller 为空");
        return;
    }

    _poller->RemoveEvent(shared_from_this());
    INF_LOG("Channel 移除事件监控");
}

void Channel::HandleEvent()
{
    const uint32_t revents = _revents;
    _revents = 0;

    if (revents == 0)
    {
        return;
    }

    // 1. 处理读事件
    if ((revents & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) != 0)
    {
        if (_read_callback)
        {
            _read_callback();
        }
    }

    // 2. 处理写事件
    if ((revents & EPOLLOUT) != 0)
    {
        if (_write_callback)
        {
            _write_callback();
        }
    }

    // 3. 本轮正常 I/O 事件处理完成后的统一处理
    if (_event_callback)
    {
        _event_callback();
    }

    // 4. 错误最后处理，因为可能关闭并释放连接
    if ((revents & EPOLLERR) != 0)
    {
        if (_error_callback)
        {
            _error_callback();
        }
        return;
    }

    // 5. 挂断最后处理
    // 如果同时有 EPOLLIN，前面已经先把剩余数据读取了
    if ((revents & EPOLLHUP) != 0)
    {
        if (_close_callback)
        {
            _close_callback();
        }
        return;
    }
}
