#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <string>
#include <netinet/in.h>
#include "eventloop.h"
#include "connection.h"
#include "log.h"
#include "utils.h"

using namespace std;

class TcpServer
{
private:
    unique_ptr<Eventloop> mainloop;         // mainloop 负责接受客户端连接
    vector<unique_ptr<Eventloop>> subloops; // subloops 负责已建立连接的读写
    vector<thread> threads;                 // 用线程为每个Reactor开辟独立执行空间
    atomic<int> nextloop_{0};               // 原子计数器，保证在多线程环境下++操作不发生数据错乱
    int listenfd;
    unordered_map<int, shared_ptr<Connection>> connecions;
    mutex mtx_; // 互斥锁

public:
    TcpServer(int port, int subReactors = 4);
    ~TcpServer();

    // 启动服务器
    void start();

private:
    void handleListen();
    void createClient(int clientfd);
    void removeConnection(int clientfd);
};

#endif
