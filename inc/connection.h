#ifndef CONNECTION_H
#define CONNECTION_H

#include <string>
#include <memory>
#include "eventloop.h"
#include "channel.h"
#include "http_parser.h"
#include "log.h"

using namespace std;

// 管理连接
class Connection
{
private:
    Eventloop &loop;
    int clientfd;
    unique_ptr<Channel> ptrChannel;
    Channel *channel;
    string inBuffer; // 读缓冲区
    string outBuffer;
    bool closed;
    bool keepAlive;

public:
    Connection(Eventloop &newloop, int newclientfd);
    ~Connection();

    void handleClose();
    void handleWrite();
    void serverRequst(const HttpRequest &req);
    void handleRead();
    void start();
};

#endif
