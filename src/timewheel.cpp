#include <stdint.h>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>

using TaskFunc = std::function<void()>;
using RelsFunc = std::function<void()>;

class TimerTask
{
private:
    uint64_t _id;      // 定时器任务的唯一标识符
    uint64_t _timeout; // 定时器任务的超时时间，单位为毫秒
    TaskFunc _task_cb; // 定时器任务的回调函数
    RelsFunc _rels_cb; // 定时器任务的释放回调函数,处理timerwheel中的定时器信息
public:
    TimerTask(uint64_t id, uint64_t timeout, TaskFunc task_cb)
        : _id(id), _timeout(timeout), _task_cb(task_cb) {}
    void SetRelsFunc(const RelsFunc &rels_cb)
    {
        _rels_cb = rels_cb;
    }
    ~TimerTask()
    {
        _task_cb();
        _rels_cb();
    }
};

class TimeWheel
{
    using PtrTask = std::shared_ptr<TimerTask>; // 定时器任务的智能指针
    using WeakTask = std::weak_ptr<TimerTask>;  // 用于保存timertask最新的shareptr，同时不会增加引用计数
private:
    std::vector<std::vector<PtrTask>> _slots;         // 时间轮的槽，每个槽存储多个定时器任务
    int _tick;                                        // 时间轮秒针，表示当前时间轮的时间位置
    int _capacity;                                    // 时间轮的容量，即槽的数量
    std::unordered_map<uint64_t, WeakTask> _task_map; // <id, WeakTask>，用于根据定时器任务的唯一标识符快速查找定时器任务
public:
    TimeWheel();
    void AddTask(uint64_t id, uint64_t timeout, TaskFunc task_cb);
    void RefreshTask(uint64_t id, uint64_t timeout);
};
