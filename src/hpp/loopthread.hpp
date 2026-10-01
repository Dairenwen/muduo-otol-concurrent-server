#pragma once
#include <thread>
#include <condition_variable>
#include <mutex>
#include "log.hpp"
#include "eventloop.hpp"

class LoopThread
{
private:
    std::mutex _mutex;             // 保护 _loop 的跨线程读写，保证发布指针时的内存可见性
    std::condition_variable _cond; // 构造函数等待子线程完成 Loop 构造；wait 会释放锁
    EventLoop *_loop;              // 非拥有指针；EventLoop 在子线程中构造、运行、销毁
    std::thread _thread;           // 最后初始化，确保线程访问的状态已全部构造

    void ThreadEntry(); // 实例化 EventLoop，发布指针，然后开始循环

public:
    LoopThread();
    ~LoopThread(); // 从所属 Loop 线程之外析构；先停止 Loop，再 join 回收线程

    // LoopThread 构造返回时 Loop 已就绪；循环已结束时返回 nullptr。
    EventLoop *GetLoop();
};
