#include "tcpserver.h"

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdlib>

TcpServer::TcpServer(int port, int subReactors) : listenfd(-1)
{
    mainloop = make_unique<Eventloop>(1024);
    for (int i = 0; i < subReactors; i++)
    {
        subloops.push_back(make_unique<Eventloop>(1024));
    }

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        LOG_ERROR("socket error");
        exit(1);
    }
    int opt = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        LOG_ERROR("setsockopt error");
        exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        LOG_ERROR("bind error");
        exit(1);
    }

    if (listen(listenfd, 128) < 0)
    {
        LOG_ERROR("listen error");
        exit(1);
    }
    setnonblock(listenfd);
    // 设置
    mainloop->set_remove_callback([this](int fd)
                                  { removeConnection(fd); });
    for (auto &subloop : subloops)
    {
        subloop->set_remove_callback([this](int fd)
                                     { removeConnection(fd); });
    }
    LOG_INFO("Server start listen on port " + to_string(port));
}

TcpServer::~TcpServer()
{
    if (listenfd >= 0)
        close(listenfd);
    for (auto &t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

// 启动服务器
void TcpServer::start()
{
    auto listenChannel = make_unique<Channel>(listenfd, EPOLLIN | EPOLLET);
    listenChannel->set_read_callback([this]
                                     { handleListen(); });

    mainloop->add_channel(move(listenChannel));
    for (size_t i = 0; i < subloops.size(); i++)
    {
        threads.emplace_back([this, i]() { // 原地构造
            subloops[i]->loop();
        });
    }

    mainloop->loop();
}

void TcpServer::handleListen()
{
    while (1)
    {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        int clientfd = accept(listenfd, reinterpret_cast<sockaddr *>(&addr), &len);

        if (clientfd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            LOG_ERROR("accept error");
            break;
        }
        setnonblock(clientfd);
        LOG_INFO("New Client connected, fd = " + to_string(clientfd));
        // 创建客户端，把剩下的事交给 createClient 处理
        createClient(clientfd);
    }
}
void TcpServer::createClient(int clientfd)
{
    Eventloop *loop = subloops[nextloop_.fetch_add(1) % subloops.size()].get();
    auto connection = make_shared<Connection>(*loop, clientfd);
    {
        lock_guard<mutex> lock(mtx_);
        connecions[clientfd] = connection;
    }
    connection->start();
}
void TcpServer::removeConnection(int clientfd)
{
    lock_guard<mutex> lock(mtx_);
    auto it = connecions.find(clientfd);
    if (it == connecions.end())
        return;
    LOG_INFO("Remove connection fd = " + to_string(clientfd));
    connecions.erase(it);
}
