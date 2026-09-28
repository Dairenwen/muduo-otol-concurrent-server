#include "timewheel.hpp"
#include "eventloop.hpp"
#include "log.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <sys/timerfd.h>
#include <unistd.h>

TimerTask::TimerTask(uint64_t id, uint64_t timeout, TaskFunc task_cb)
    : _id(id), _timeout(timeout), _task_cb(task_cb), _canceled(false)
{
}

TimeWheel::TimeWheel(EventLoop *loop)
    : _loop(loop), _timerfd(-1), _capacity(60), _tick(0), _slots(_capacity)
{
    if (_loop == nullptr)
    {
        ERR_LOG("TimeWheel 创建失败：EventLoop 为空");
        throw std::invalid_argument("TimeWheel requires an EventLoop");
    }
    INF_LOG("TimeWheel 已绑定 EventLoop");

    _timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (_timerfd < 0)
    {
        ERR_LOG("创建 timerfd 失败，errno=%d，error=%s", errno, std::strerror(errno));
        return;
    }

    struct itimerspec itime{};
    itime.it_interval.tv_sec = 1;
    itime.it_interval.tv_nsec = 0; // 设置第一次超时之后间隔
    itime.it_value.tv_nsec = 0;
    itime.it_value.tv_sec = 1;
    timerfd_settime(_timerfd, 0, &itime, nullptr);

    // timerfd 到期会变成可读；Channel 将这个可读事件交给 EventLoop 的 Poller 监听。
    _timer_channel = std::make_shared<Channel>(_loop->GetPoller(), _timerfd);
    _timer_channel->SetReadCallbck([this]()
                                   {
                                       // 先读取 timerfd：清除可读状态，并取出上次读取后累计的到期次数。
                                       uint64_t expirations = 0;
                                       ssize_t bytes;
                                       do
                                       {
                                           bytes = read(_timerfd, &expirations, sizeof(expirations));
                                       } while (bytes == -1 && errno == EINTR); // 被信号中断时重试

                                       // 只有完整读出 8 字节，expirations 才是有效的到期次数。
                                       // 暂时无数据时直接返回；其他读取异常只记录错误，不推进时间轮。
                                       if (bytes != static_cast<ssize_t>(sizeof(expirations)))
                                       {
                                           if (bytes != -1 || (errno != EAGAIN && errno != EWOULDBLOCK))
                                               ERR_LOG("读取 timerfd 失败，errno=%d，error=%s", errno, std::strerror(errno));
                                           return;
                                       }

                                       // EventLoop 忙碌时可能错过多个秒点；每到期一次就推进一格，避免丢失时间。
                                       for (uint64_t tick = 0; tick < expirations; ++tick)
                                           RunTimerTask(); });
    // 注册可读事件后，EventLoop 收到 timerfd 通知才会调用上面的回调。
    _timer_channel->EnableRead();
    _timer_channel->Update();
}

void TimerTask::SetRelsFunc(const RelsFunc &rels_cb)
{
    _rels_cb = rels_cb;
}

void TimerTask::SetCanceled()
{
    _canceled = true;
}

uint64_t TimerTask::GetDelayTime()
{
    return _timeout;
}

TimerTask::~TimerTask()
{
    if (!_canceled && _task_cb)
        _task_cb(); // 到时间执行任务

    if (_rels_cb)
        _rels_cb();
}

void TimeWheel::RemoveTimer(uint64_t id)
{
    if (_task_map.find(id) != _task_map.end())
    {
        _task_map.erase(id);
    }
}

// 防止其他线程引发线程安全问题，将任务的添加、刷新、取消和执行都放在 EventLoop 所在线程中完成。
void TimeWheel::AddTask(uint64_t id, uint64_t timeout, TaskFunc task_cb)
{
    // 其他线程只投递操作；槽位和任务表统一由 EventLoop 所在线程修改。
    _loop->RunInLoop([this, id, timeout, task_cb]()
                     { AddTaskInLoop(id, timeout, task_cb); });
}

void TimeWheel::AddTaskInLoop(uint64_t id, uint64_t timeout, TaskFunc task_cb)
{
    PtrTask pt(new TimerTask(id, timeout, task_cb));
    pt->SetRelsFunc(std::bind(&TimeWheel::RemoveTimer, this, id));
    _task_map[id] = WeakTask(pt);                        // 注意哈希表中保存的是weakptr
    _slots[(_tick + timeout) % _capacity].push_back(pt); // 注意时间轮为循环数组
}

void TimeWheel::RefreshTask(uint64_t id)
{
    _loop->RunInLoop([this, id]()
                     { RefreshTaskInLoop(id); });
}

void TimeWheel::RefreshTaskInLoop(uint64_t id)
{
    if (_task_map.find(id) != _task_map.end())
    {
        PtrTask pt = _task_map[id].lock(); // 根据weakptr构造shareptr，引用计数+1
        _slots[(_tick + pt->GetDelayTime()) % _capacity].push_back(pt);
    }
}

void TimeWheel::CancelTask(uint64_t id)
{
    _loop->RunInLoop([this, id]()
                     { CancelTaskInLoop(id); });
}

void TimeWheel::CancelTaskInLoop(uint64_t id)
{
    if (_task_map.find(id) != _task_map.end())
    {
        PtrTask pt = _task_map[id].lock(); // 根据weakptr构造shareptr，引用计数+1，但出作用域销毁，计数-1不影响
        if (pt)
            pt->SetCanceled();
    }
}

void TimeWheel::RunTimerTask()
{
    _loop->RunInLoop([this]()
                     { RunTimerTaskInLoop(); });
}

void TimeWheel::RunTimerTaskInLoop()
{
    // 每秒都要执行到时的任务
    _tick = (_tick + 1) % _capacity;
    // 清空当前槽位的任务，每个任务sharedptr-1，如果为0，触发析构函数执行任务
    _slots[_tick].clear();
}

bool TimeWheel::HasTimer(uint64_t id)
{
    if (_task_map.find(id) != _task_map.end())
        return true;
    else
        return false;
}

TimeWheel::~TimeWheel()
{
    if (_timer_channel)
    {
        _timer_channel->DisableAll();
        _timer_channel->Remove();
        _timer_channel.reset();
    }
    if (_timerfd != -1)
    {
        close(_timerfd);
        _timerfd = -1;
    }

    // 先取消：析构 TimerTask 时不会错误执行超时业务回调。
    for (auto &slot : _slots)
    {
        for (auto &task : slot)
        {
            if (task)
                task->SetCanceled();
        }
    }

    _slots.clear();
    _task_map.clear();
}
