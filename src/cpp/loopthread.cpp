#include "loopthread.hpp"
#include <exception>

LoopThread::LoopThread()
    : _loop(nullptr), _thread()
{
    // 创建线程便会启动 ThreadEntry
    INF_LOG("LoopThread 正在创建工作线程");
    _thread = std::thread(&LoopThread::ThreadEntry, this);
    std::unique_lock<std::mutex> lock(_mutex);
    _cond.wait(lock, [this]()
               { return _loop != nullptr; });
    DBG_LOG("LoopThread 工作线程和 EventLoop 已就绪");
}

LoopThread::~LoopThread()
{
    if (!_thread.joinable()) // 子线程是否还需要回收
        return;

    // 子线程无法 join 自己，也不能 detach 后继续访问已销毁的 this。
    if (_thread.get_id() == std::this_thread::get_id())
    {
        FATAL_LOG("LoopThread 不能在自己的 Loop 线程中析构");
        std::terminate();
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_loop != nullptr)
        {
            INF_LOG("LoopThread 请求停止 EventLoop");
            // 持锁期间子线程不能撤销 _loop，保证调用停止接口时对象仍存活。
            _loop->StopEventLoop();
        }
    }

    // join 时不能持有 _mutex
    _thread.join();
    INF_LOG("LoopThread 工作线程已回收");
}

void LoopThread::ThreadEntry()
{
    // 局部对象在子线程构造，因此 EventLoop 记录的线程 ID 就是工作线程。
    EventLoop loop;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _loop = &loop; // 构造完成才发布；GetLoop 用同一把锁读取。
    }
    _cond.notify_all();
    INF_LOG("LoopThread EventLoop 已发布，准备进入事件循环");
    loop.StartEventLoop();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _loop = nullptr;
    }

    INF_LOG("LoopThread EventLoop 已销毁，子线程退出");
}

EventLoop *LoopThread::GetLoop()
{
    // 加锁读取，防止与退出时清空指针冲突
    std::lock_guard<std::mutex> lock(_mutex);
    DBG_LOG("LoopThread GetLoop 返回 EventLoop 指针");
    return _loop;
}
