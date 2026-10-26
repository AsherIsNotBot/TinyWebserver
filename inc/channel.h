#ifndef CHANNEL_H
#define CHANNEL_H

#include <stdint.h>
#include <functional>
#include <sys/epoll.h>

using namespace std;

// channel 类 (根据事件调用相关 call_back)

class Channel
{
private:
    int fd;
    uint32_t event;
    bool active;
    function<void()> read_callback;
    function<void()> write_callback;
    function<void()> close_callback;

public:
    Channel(int newfd);
    Channel(int newfd, uint32_t newevent);

    int getfd();
    uint32_t getevent();
    void setevent(uint32_t newevent);
    void disable();
    void set_read_callback(function<void()> cb);
    void set_write_callback(function<void()> cb);
    void set_close_callback(function<void()> cb);
    void handle_event(uint32_t ev);
};

#endif
