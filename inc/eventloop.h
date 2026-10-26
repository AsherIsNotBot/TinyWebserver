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

using namespace std;

class Eventloop
{
private:
    int epfd;
    vector<epoll_event> events;
    unordered_map<int, unique_ptr<Channel>> channels;
    unordered_set<int> peddingRemove;
    function<void(int)> remove_callback; // 这个回调函数具体实现在哪

    // 处理 peddingRemove
    void handle_peddingRemove();

public:
    Eventloop(int maxlen);
    ~Eventloop();

    void set_remove_callback(function<void(int)> cb);
    void remove_channel(int fd);
    void add_channel(unique_ptr<Channel> channel);
    void update_channel(Channel *channel);
    void loop();
};

#endif
