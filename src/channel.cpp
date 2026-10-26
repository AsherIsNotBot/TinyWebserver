#include "channel.h"

Channel::Channel(int newfd) : fd(newfd), event(0), active(true) {}
Channel::Channel(int newfd, uint32_t newevent) : fd(newfd), event(newevent), active(true) {}

int Channel::getfd()
{
    return fd;
}
uint32_t Channel::getevent()
{
    return event;
}
void Channel::setevent(uint32_t newevent)
{
    event = newevent;
}
void Channel::disable()
{
    active = false;
    event = 0;
}
void Channel::set_read_callback(function<void()> cb)
{
    read_callback = move(cb);
}
void Channel::set_write_callback(function<void()> cb)
{
    write_callback = move(cb);
}
void Channel::set_close_callback(function<void()> cb)
{
    close_callback = move(cb);
}
void Channel::handle_event(uint32_t ev)
{
    if (!active)
    {
        // 如果已经请求删除删除，不执行事件
        return;
    }
    if ((ev & EPOLLIN) && read_callback)
    {
        read_callback();
        if (!active)
        {
            // 如果在 read_callback 里调用close
            return;
        }
    }
    if ((ev & EPOLLOUT) && write_callback)
    {
        write_callback();
        if (!active)
        {
            // 如果在 read_callback 里调用close
            return;
        }
    }
    if ((ev & (EPOLLHUP | EPOLLERR)) && close_callback)
    {
        close_callback();
    }
}
