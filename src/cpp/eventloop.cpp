#include "eventloop.hpp"
#include "log.hpp"

EventLoop::EventLoop()
    : _thread_id(std::this_thread::get_id()),
      _eventfd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
      _poller(),
      _channel(nullptr),
      _time_wheel(this)
{
    if (_eventfd == -1)
    {
        throw std::runtime_error(std::string("创建 eventfd 失败: ") + std::strerror(errno));
    }

    // eventfd 可读表示有线程向任务队列中加入了任务。读取一次会清空其计数器，从而将该通知消费掉；真正的任务由 RunTask() 在本轮末尾执行。
    _channel = std::make_shared<Channel>(&_poller, _eventfd);
    _channel->SetReadCallbck([this]()
                             {
                                 uint64_t pending_count = 0;
                                 while (read(_eventfd, &pending_count, sizeof(pending_count)) == -1)
                                 {
                                     if (errno == EINTR)
                                     {
                                         continue; // 被信号打断，继续完成这次读取。
                                     }
                                     if (errno != EAGAIN && errno != EWOULDBLOCK)
                                     {
                                         ERR_LOG("读取 eventfd 失败: errno=%d, error=%s", errno, std::strerror(errno));
                                     }
                                     break; // 非阻塞 eventfd 已清空或读取失败。
                                 } });
    _channel->EnableRead();
    UpdateEventfd(_channel.get()); // 返回原始指针，不会增加引用计数
}

EventLoop::~EventLoop()
{
    // 先取消 epoll 监听，再关闭 fd，避免 Poller 的 fd -> Channel 映射保留失效 fd。
    if (_channel)
    {
        _channel->DisableAll();
        RemoveEventfd(_channel.get());
        _channel.reset();
    }

    if (_eventfd != -1)
    {
        close(_eventfd);
        _eventfd = -1;
    }
}

void EventLoop::RunInLoop(const Functor &cb)
{
    if (!cb)
    {
        return;
    }

    // Loop 线程无需排队，可以立即执行，避免一次不必要的 epoll 唤醒。
    if (IsInLoopThread())
    {
        cb();
        return;
    }

    QueueInLoop(cb);
}

void EventLoop::QueueInLoop(const Functor &task)
{
    if (!task)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _tasks.push_back(task);
    }

    // 每次入队都通知。即使当前就在 Loop 线程中，这也能保证 RunTask() 执行期间新入队的任务会使下一轮 epoll_wait 立即返回。
    const uint64_t one = 1;
    while (write(_eventfd, &one, sizeof(one)) == -1)
    {
        if (errno == EINTR)
        {
            continue;
        }
        // EAGAIN 仅代表 eventfd 的计数器已满；此时它已经处于可读状态，EventLoop 一定会被唤醒，因此无需重复写入。
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            ERR_LOG("写入 eventfd 失败: errno=%d, error=%s", errno, std::strerror(errno));
        }
        break;
    }
}

bool EventLoop::IsInLoopThread() const
{
    return _thread_id == std::this_thread::get_id();
}

void EventLoop::UpdateEventfd(Channel *ch)
{
    if (ch == nullptr)
    {
        ERR_LOG("更新 eventfd 失败：Channel 为空");
        return;
    }
    ch->Update();
}

void EventLoop::RemoveEventfd(Channel *ch)
{
    if (ch == nullptr)
    {
        return;
    }
    ch->Remove();
}

void EventLoop::StartEventLoop()
{
    std::vector<Poller::ChannelPtr> active_channels;
    for (;;)
    {
        // 1. 等待 fd 发生事件
        _poller.Poll(active_channels);

        // 2. 处理本轮发生事件的 fd
        for (const auto &channel : active_channels)
        {
            channel->HandleEvent();
        }

        // 3. 执行 EventLoop 任务队列
        RunTask();
    }
}

void EventLoop::RunTask()
{
    std::vector<Functor> tasks;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        // 回调可以继续投递任务，其他线程也不会因耗时任务长期阻塞在互斥锁上。
        tasks.swap(_tasks);
    }

    for (const auto &task : tasks)
    {
        if (task)
        {
            task();
        }
    }
}

void EventLoop::AddTask(uint64_t id, uint64_t timeout, TaskFunc task_cb)
{
    _time_wheel.AddTask(id, timeout, task_cb);
}

void EventLoop::RefreshTask(uint64_t id)
{
    _time_wheel.RefreshTask(id);
}

void EventLoop::CancelTask(uint64_t id)
{
    _time_wheel.CancelTask(id);
}

bool EventLoop::HasTimer(uint64_t id)
{
    return _time_wheel.HasTimer(id);
}
