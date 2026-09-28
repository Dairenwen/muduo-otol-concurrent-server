#pragma once
#include <sys/eventfd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <utility>
#include "timewheel.hpp"
#include <functional>
#include <mutex>
#include <memory>
#include "channel.hpp"
#include "poller.hpp"
#include <vector>
#include <thread>

class EventLoop
{
    using Functor = std::function<void()>;

private:
    std::thread::id _thread_id;        // 在eventloop线程中直接执行任务，不是eventloop线程中则将任务添加到任务队列中
    int _eventfd;                      // eventfd的文件描述符
    Poller _poller;                    // Poller 也会保存 Channel 的 shared_ptr；
    std::shared_ptr<Channel> _channel; // 专门监听 eventfd 的唤醒 Channel
    std::vector<Functor> _tasks;       // 保存需要执行的任务
    std::mutex _mutex;                 // 互斥锁，保护任务队列
    TimeWheel _time_wheel;

public:
    EventLoop();
    ~EventLoop();

    void RunInLoop(const Functor &cb);     // 运行事件循环
    void QueueInLoop(const Functor &task); // 将任务添加到任务队列中
    bool IsInLoopThread() const;           // 判断当前线程是否是事件循环所在的线程
    Poller *GetPoller() { return &_poller; }
    void UpdateEventfd(Channel *ch); // 将 eventfd Channel 的当前事件集同步到 Poller
    void RemoveEventfd(Channel *ch); // 从 Poller 移除 eventfd Channel 的监控
    void StartEventLoop();           // 启动事件循环，监控->就绪事件处理->执行任务->继续监控
    void RunTask();                  // 执行任务队列中的任务
    void AddTask(uint64_t id, uint64_t timeout, TaskFunc task_cb);
    void RefreshTask(uint64_t id);
    void CancelTask(uint64_t id);
    bool HasTimer(uint64_t id);
};
