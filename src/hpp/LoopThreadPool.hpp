#pragma once
#include "loopthread.hpp"
#include <memory>
#include <stdexcept>
#include <cassert>
#include <vector>

class LoopThreadPool
{
private:
    int _thread_count;    // 线程数量
    int _next_thread_idx; // RR分配给从reactor下标

    EventLoop *_mainloop;               // 主eventloop
    std::vector<LoopThread *> _threads; // 保存所有loopthread
    std::vector<EventLoop *> _loops;    // 保存所有loop
public:
    LoopThreadPool(EventLoop *mainloop);
    ~LoopThreadPool();
    void SetThreadCount(int count); //  设置线程数量
    void Create();                  // 创建所有的从属线程
    EventLoop *NextLoop();          // 获取下一个要分配的loop
};
