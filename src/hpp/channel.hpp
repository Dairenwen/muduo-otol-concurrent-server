#pragma once
#include <sys/epoll.h>
#include <functional>
#include <memory>

class Poller;
class Channel : public std::enable_shared_from_this<Channel>
{
    using EventCallback = std::function<void()>;

private:
    int _socket_fd;
    Poller *_poller;   // 指向所属的 Poller 对象，便于在事件处理时访问 Poller 的功能
    uint32_t _events;  // 当前需要监控的事件
    uint32_t _revents; // 当前连接触发的事件

    EventCallback _read_callback;  // 可读事件被触发的回调函数
    EventCallback _write_callback; // 可写事件被触发的回调函数
    EventCallback _error_callback; // 错误事件被触发的回调函数
    EventCallback _close_callback; // 连接断开事件被触发的回调函数
    EventCallback _event_callback; // 任意事件被触发的回调函数

public:
    Channel() : _poller(nullptr), _socket_fd(-1), _events(0), _revents(0) {}
    Channel(Poller *poller, int socket_fd) : _poller(poller), _socket_fd(socket_fd), _events(0), _revents(0) {}
    int GetSocketFd() const;
    uint32_t GetEvents() const;
    void SetReadCallbck(const EventCallback &cb);
    void SetWriteCallbck(const EventCallback &cb);
    void SetErrorCallbck(const EventCallback &cb);
    void SetCloseCallbck(const EventCallback &cb);
    void SetEventCallbck(const EventCallback &cb);

    bool ReadAble();  // 当前是否可读
    bool WriteAble(); // 当前是否可写

    void EnableRead();  // 启动读事件监控
    void EnableWrite(); // 启动写事件监控

    void DisableRead();  // 关闭读事件监控
    void DisableWrite(); // 关闭写事件监控
    void DisableAll();   // 关闭所有事件监控

    void Update();                     // 更新事件监控状态，通知 Poller 对象
    void Remove();                     // 从 Poller 中移除当前 Channel 的事件监控
    void SetRevents(uint32_t revents); // 由eventloop中的epoll设置触发事件

    void HandleEvent(); // 事件处理，一旦连接触发了事件，就调用这个函数，自己触发了什么事件如何处理自己决定
};
