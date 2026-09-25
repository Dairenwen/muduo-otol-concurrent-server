#include "poller.hpp"
#include "channel.hpp"
#include "log.hpp"

Poller::Poller()
    : _epoll_fd(epoll_create1(EPOLL_CLOEXEC)), // 创建 epoll 实例，EPOLL_CLOEXEC 确保在 exec 系统调用时关闭 epoll 文件描述符
      _events(MAX_EPOLLEVENTS)
{
    if (_epoll_fd == -1)
    {
        ERR_LOG("创建 epoll 失败: errno = %d, error = %s", errno, std::strerror(errno));
        return;
    }

    INF_LOG("Poller 创建成功，epoll fd = %d", _epoll_fd);
}

Poller::~Poller()
{
    if (_epoll_fd != -1)
    {
        close(_epoll_fd);
        DBG_LOG("Poller 关闭，epoll fd = %d", _epoll_fd);
    }
}

bool Poller::HasChannel(const ChannelPtr &channel)
{
    if (channel == nullptr)
        return false;

    auto it = _channels.find(channel->GetSocketFd());
    return it != _channels.end() && it->second == channel;
}

void Poller::Update(const ChannelPtr &channel, int op)
{
    if (_epoll_fd == -1 || channel == nullptr)
    {
        ERR_LOG("更新 epoll 失败：epoll fd 或 Channel 无效");
        return;
    }

    epoll_event event{};
    event.events = channel->GetEvents();
    event.data.fd = channel->GetSocketFd();

    if (epoll_ctl(_epoll_fd, op, event.data.fd, op == EPOLL_CTL_DEL ? nullptr : &event) == -1)
    {
        ERR_LOG("epoll_ctl 失败: op=%d, fd=%d, errno=%d, error=%s",
                op, event.data.fd, errno, std::strerror(errno));
        return;
    }

    if (op == EPOLL_CTL_DEL)
        _channels.erase(event.data.fd);
    else
        _channels[event.data.fd] = channel;
}

void Poller::UpdateEvent(const ChannelPtr &channel)
{
    if (channel == nullptr)
        return;

    Update(channel, HasChannel(channel) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD);
}

void Poller::RemoveEvent(const ChannelPtr &channel)
{
    if (channel == nullptr || !HasChannel(channel))
        return;

    Update(channel, EPOLL_CTL_DEL);
}

void Poller::Poll(std::vector<ChannelPtr> &active)
{
    active.clear();                                                                                      // 清空活跃列表，准备接收新的活跃 Channel
    const int event_count = epoll_wait(_epoll_fd, _events.data(), static_cast<int>(_events.size()), -1); // 会改变event数组，活跃的事件放到events数组前方

    if (event_count <= 0)
        return;

    for (int i = 0; i < event_count; ++i)
    {
        const int fd = _events[i].data.fd;
        auto it = _channels.find(fd);
        if (it == _channels.end())
            continue;

        it->second->SetRevents(_events[i].events);
        active.push_back(it->second); // 活跃列表也持有对象，回调中 Remove 不会立即析构。
    }
}
