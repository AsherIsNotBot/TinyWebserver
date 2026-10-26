#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include "channel.h"
#include "log.h"
#include "timer.h"

using namespace std;

class Eventloop
{
private:
    int epfd;
    vector<epoll_event> events;
    unordered_map<int, unique_ptr<Channel>> channels;
    unordered_set<int> peddingRemove;
    function<void(int)> remove_callback; // 这个回调函数具体实现在哪
    HeapTimer timer_;                    // 小根堆定时器，负责踢掉空闲超时的连接

    // 处理 peddingRemove
    void handle_peddingRemove();

public:
    Eventloop(int maxlen);
    ~Eventloop();

    void set_remove_callback(function<void(int)> cb);
    void remove_channel(int fd);
    void add_channel(unique_ptr<Channel> channel);
    void update_channel(Channel *channel);
    // 供 Connection 调用的定时器刷新接口（惰性更新，只改过期时间不重排）
    void refresh_timer(int fd, int timeout_ms, function<void()> cb);
    void loop();
};

#endif
