#include "eventloop.h"

#include <cstdlib>
#include <string>

// 处理 peddingRemove
void Eventloop::handle_peddingRemove()
{
    for (int fd : peddingRemove)
    {
        auto it = channels.find(fd);
        if (it == channels.end())
            continue;

        // 从epoll中删除fd
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) < 0)
        {
            LOG_ERROR("epoll_ctl delete error");
            exit(1);
        }
        // 从EventLoop中删除fd
        channels.erase(fd);
        // 通知TcpServer，fd已经安全删除
        if (remove_callback)
        {
            remove_callback(fd);
        }
    }
    peddingRemove.clear();
}

Eventloop::Eventloop(int maxlen) : events(maxlen)
{
    epfd = epoll_create1(0);
    if (epfd < 0)
    {
        LOG_ERROR("epoll_create1 error");
        exit(1);
    }
}
Eventloop::~Eventloop()
{
    close(epfd);
}

void Eventloop::set_remove_callback(function<void(int)> cb)
{
    remove_callback = move(cb);
}
void Eventloop::remove_channel(int fd)
{
    peddingRemove.insert(fd);
}
void Eventloop::add_channel(unique_ptr<Channel> channel)
{
    Channel *ptr = channel.get();

    int fd = channel->getfd();
    epoll_event event{};
    event.data.ptr = ptr; // 这里先get，让event拿到channel的真实地址，再move转移所有权，防止ptr失效
    event.events = channel->getevent();

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event) < 0)
    {
        LOG_ERROR("epoll_ctl add error");
        exit(1);
    }
    channels[fd] = move(channel);
}

void Eventloop::update_channel(Channel *channel)
{
    epoll_event event{};
    event.data.ptr = channel;
    event.events = channel->getevent();
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, channel->getfd(), &event) < 0)
    {
        LOG_ERROR("update_channel error");
        exit(1);
    }
}

void Eventloop::loop()
{
    while (1)
    {
        int n = epoll_wait(epfd, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            LOG_ERROR("epoll_wait error");
            break;
        }
        for (int i = 0; i < n; i++)
        {
            Channel *channel = static_cast<Channel *>(events[i].data.ptr);
            int fd = channel->getfd();
            // 如果fd已经存在 peddingRemove中跳过
            if (peddingRemove.count(fd))
                continue;
            channel->handle_event(events[i].events);
        }
        handle_peddingRemove();
    }
}
