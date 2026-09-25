#pragma once
#include <sys/epoll.h>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <unordered_map>
#include <memory>
#include <vector>
#define MAX_EPOLLEVENTS 1024

class Channel; // 前置声明

class Poller
{
public:
    using ChannelPtr = std::shared_ptr<Channel>; // 防止Channel 被提前析构，导致 Poller 中的 fd -> Channel 映射悬空

private:
    int _epoll_fd;                                 // epoll 操作句柄
    std::vector<epoll_event> _events;              // 保存 epoll_wait 返回的就绪事件
    std::unordered_map<int, ChannelPtr> _channels; // fd -> Channel

    void Update(const ChannelPtr &channel, int op); // 对epoll的直接操作
    bool HasChannel(const ChannelPtr &channel);     // 是否已经添加监控
public:
    Poller();
    ~Poller();

    Poller(const Poller &) = delete;
    Poller &operator=(const Poller &) = delete;

    void UpdateEvent(const ChannelPtr &channel); // 添加或修改监控事件
    void RemoveEvent(const ChannelPtr &channel); // 移除监控
    void Poll(std::vector<ChannelPtr> &active);  // 开始监控
};
