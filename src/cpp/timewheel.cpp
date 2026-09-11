#include "timewheel.hpp"

TimerTask::TimerTask(uint64_t id, uint64_t timeout, TaskFunc task_cb)
    : _id(id), _timeout(timeout), _task_cb(task_cb), _canceled(false)
{
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
    if (!_canceled)
        _task_cb(); // 到时间执行任务
    _rels_cb();
}

void TimeWheel::RemoveTimer(uint64_t id)
{
    if (_task_map.find(id) != _task_map.end())
    {
        _task_map.erase(id);
    }
}

TimeWheel::TimeWheel() : _capacity(60), _tick(0), _slots(_capacity)
{
}

void TimeWheel::AddTask(uint64_t id, uint64_t timeout, TaskFunc task_cb)
{
    PtrTask pt(new TimerTask(id, timeout, task_cb));
    pt->SetRelsFunc(std::bind(&TimeWheel::RemoveTimer, this, id));
    _task_map[id] = WeakTask(pt);                        // 注意哈希表中保存的是weakptr
    _slots[(_tick + timeout) % _capacity].push_back(pt); // 注意时间轮为循环数组
}

void TimeWheel::RefreshTask(uint64_t id)
{
    if (_task_map.find(id) != _task_map.end())
    {
        PtrTask pt = _task_map[id].lock(); // 根据weakptr构造shareptr，引用计数+1
        _slots[(_tick + pt->GetDelayTime()) % _capacity].push_back(pt);
    }
}

void TimeWheel::CancelTask(uint64_t id)
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
    // 每秒都要执行到时的任务
    _tick = (_tick + 1) % _capacity;
    // 清空当前槽位的任务，每个任务sharedptr-1，如果为0，触发析构函数执行任务
    _slots[_tick].clear();
}
