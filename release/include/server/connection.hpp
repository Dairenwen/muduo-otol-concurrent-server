#pragma once
#include "any.hpp"
#include "buffer.hpp"
#include "channel.hpp"
#include "socket.hpp"
#include "eventloop.hpp"
#include <functional>
#include <memory>

typedef enum
{
    CONNECTING,
    CONNECTED,
    DISCONNECTING,
    DISCONNECTED
} ConnStatu;

class Connection;
using ConnPtr = std::shared_ptr<Connection>;

class Connection : public std::enable_shared_from_this<Connection>
{
public:
    using ConnectedCallback = std::function<void(const ConnPtr &)>;
    using MessageCallback = std::function<void(const ConnPtr &, Buffer &)>;
    using CloseCallback = std::function<void(const ConnPtr &)>;
    using AnyCallback = std::function<void(const ConnPtr &)>;

private:
    uint64_t _conn_id;                 // 连接ID同时也是任务ID
    Socket _socket;                    // 负责关闭 fd
    std::shared_ptr<Channel> _channel; // channel由shareptr管理
    Buffer _In_buffer;                 // 输入缓冲区
    Buffer _Out_buffer;                // 输出缓冲区
    Any _context;                      // 保存当前协议的上下文
    ConnStatu _statu;
    EventLoop *_loop; // 一个连接对应一个loop归属于一个线程
    bool _enable_inactive_close;

    ConnectedCallback _connected_callback;
    MessageCallback _message_callback;
    CloseCallback _close_callback;
    CloseCallback _server_close_callback;
    AnyCallback _any_callback;

    void Release(); // 实际的释放接口
    void HandleAny();
    void EstablishedInLoop();
    void SendInLoop(const std::string &msg);
    void ShutdownInLoop();
    void SetInactiveCloseInLoop(bool enable, uint64_t sec);
    void SwitchProtocolInloop(const Any &context, const ConnectedCallback &conn,
                              const MessageCallback &msg, const CloseCallback &closed,
                              const AnyCallback &event);

public:
    Connection(EventLoop *loop, int fd, uint64_t conn_id);
    ~Connection();

    // 以下函数由 Channel 的回调调用，在所属 EventLoop 线程中执行。
    void Established();
    void HandleRead();
    void HandleWrite();
    void HandleClose();

    void Send(const std::string &msg);
    void Shutdown(); // 待输出缓冲区发送完后关闭连接

    // 启动前设置回调；启动后只能在 Loop 线程中修改。
    void SetConnectedCallback(const ConnectedCallback &cb);
    void SetMessageCallback(const MessageCallback &cb);
    void SetCloseCallback(const CloseCallback &cb);
    void SetAnyCallback(const AnyCallback &cb);
    void SetServerCloseCallback(const CloseCallback &cb);

    // 单位为秒
    void SetInactiveClose(bool enable, uint64_t sec);
    // 常用于http升级到websocket
    void SwitchProtocol(const Any &context, const ConnectedCallback &conn,
                        const MessageCallback &msg, const CloseCallback &closed,
                        const AnyCallback &event);

    int GetSocketfd() const;
    uint64_t GetConnId() const;
    ConnStatu GetConnStatu() const;
    Buffer &GetInBuffer();
    Buffer &GetOutBuffer();
    Any &GetContext();
};
