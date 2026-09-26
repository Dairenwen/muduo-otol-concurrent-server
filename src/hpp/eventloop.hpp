#pragma once
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdint>
#include <functional>
#include <mutex>
#include "channel.hpp"
#include "poller.hpp"
#include <vector>
#include <thread>

class EventLoop
{
    using Functor = std::function<void()>;

private:
    std::thread::id _thread_id; // 在eventloop线程中直接执行任务，不是eventloop线程中则将任务添加到任务队列中
    int _eventfd;               // eventfd的文件描述符
    Poller *_poller;
    std::vector<Functor> _tasks; // 保存需要执行的任务
    std::mutex _mutex;           // 互斥锁，保护任务队列
public:
    EventLoop();
    ~EventLoop();

    void RunInLoop(const Functor &cb);     // 运行事件循环
    void QueueInLoop(const Functor &task); // 将任务添加到任务队列中
    bool IsInLoopThread() const;           // 判断当前线程是否是事件循环所在的线程
    void UpdateEventfd(Channel *ch);       // 更新eventfd，通知事件循环有新的任务需要处理
    void RemoveEventfd(Channel *ch);       // 移除eventfd，关闭文件描述符
    void StartEventLoop();                 // 启动事件循环，监控->就绪事件处理->执行任务->继续监控
    void RunTask();                        // 执行任务队列中的任务
};
