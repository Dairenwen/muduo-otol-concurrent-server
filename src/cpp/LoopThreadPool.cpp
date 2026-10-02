#include "LoopThreadPool.hpp"

LoopThreadPool::LoopThreadPool(EventLoop *mainloop)
    : _thread_count(0), _next_thread_idx(0), _mainloop(mainloop)
{
    INF_LOG("LoopThreadPool 已初始化");
}

LoopThreadPool::~LoopThreadPool()
{
    INF_LOG("LoopThreadPool 开始回收 %zu 个工作线程", _threads.size());
    _loops.clear();
    for (LoopThread *thread : _threads)
    {
        // LoopThread 析构会请求停止 EventLoop，并 join 等待工作线程退出。
        delete thread;
    }
    _threads.clear();
    INF_LOG("LoopThreadPool 工作线程已全部回收");
}

void LoopThreadPool::SetThreadCount(int count)
{
    // 配置、创建及轮询分配统一在主 Reactor 线程执行，不支持并发修改。
    if (count < 0)
    {
        ERR_LOG("LoopThreadPool 线程数量不能为负数，count=%d", count);
        return;
    }
    if (!_threads.empty())
    {
        ERR_LOG("LoopThreadPool 已创建工作线程，不能再修改线程数量");
        return;
    }

    _thread_count = count;
    INF_LOG("LoopThreadPool 配置工作线程数量：%d", _thread_count);
}

void LoopThreadPool::Create()
{
    if (!_threads.empty())
    {
        WARN_LOG("LoopThreadPool 工作线程已创建，忽略重复 Create");
        return;
    }
    if (_thread_count == 0)
    {
        INF_LOG("LoopThreadPool 不创建工作线程，分配连接时需要主 EventLoop");
        return;
    }

    INF_LOG("LoopThreadPool 开始创建 %d 个工作线程", _thread_count);
    // 预留空间：创建线程后，保存两个原始指针时无需再次扩容。
    for (int i = 0; i < _thread_count; ++i)
    {
        LoopThread *tmp = new LoopThread();
        EventLoop *loop = tmp->GetLoop();
        if (loop == nullptr || tmp == nullptr)
        {
            WARN_LOG("%d 号线程创建失败，继续尝试创建下一个", i + 1);
            continue;
        }

        _threads.push_back(tmp);
        _loops.push_back(loop);
        INF_LOG("LoopThreadPool 第 %d 个工作线程及其 EventLoop 已就绪", i + 1);
    }
    INF_LOG("LoopThreadPool 创建完成，工作线程数量=%zu", _threads.size());
}

EventLoop *LoopThreadPool::NextLoop()
{
    if (_thread_count == 0)
    {
        assert(_mainloop != nullptr);
        DBG_LOG("LoopThreadPool 无工作线程，选择主 EventLoop");
        return _mainloop;
    }
    if (_loops.empty())
    {
        ERR_LOG("LoopThreadPool 尚未创建工作线程，请先调用 Create");
        return nullptr;
    }

    _next_thread_idx = (_next_thread_idx + 1) % _thread_count;
    DBG_LOG("LoopThreadPool 为新连接选择工作 Loop，下标=%d", _next_thread_idx);
    return _loops[_next_thread_idx];
}
