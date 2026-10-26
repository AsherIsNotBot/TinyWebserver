#include "connection.h"

#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include "utils.h"

Connection::Connection(Eventloop &newloop, int newclientfd) : loop(newloop),
                                                              clientfd(newclientfd), closed(false), keepAlive(false)
{
    ptrChannel = make_unique<Channel>(newclientfd, EPOLLIN | EPOLLET);
    channel = ptrChannel.get();
    closed = false;
}
Connection::~Connection()
{
    if (clientfd >= 0)
    {
        close(clientfd);
        clientfd = -1;
    }
}
void Connection::handleClose()
{    if (closed)
        return;
    LOG_INFO("Client closed , fd = " + to_string(clientfd));
    // close(clientfd);
    closed = true;
    // 逻辑上标注remove
    channel->disable();
    // 加入peddingremove
    loop.remove_channel(clientfd);
}

void Connection::extendLife()
{
    if (closed)
        return;
    // 每次读写活动都重置倒计时（只更新堆中的过期时间，不重建节点）
    loop.refresh_timer(clientfd, TIMEOUT_MS, [this]
                       {
                           LOG_INFO("Connection timeout , kicking zombie client fd = " + to_string(clientfd));
                           handleClose();
                       });
}

void Connection::handleWrite()
{
    extendLife(); // 更新倒计时
    while (!outBuffer.empty())
    {
        ssize_t n = send(clientfd, outBuffer.data(), outBuffer.size(),
                         // 防止 对端关闭 导致 SIGPIPE
                         MSG_NOSIGNAL);

        if (n > 0)
        {
            LOG_INFO("send " + to_string(n) + " bytes to fd " + to_string(clientfd));
            // 删除已发的数据
            outBuffer.erase(0, n);
            continue;
        }
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
        }

        LOG_ERROR("send error");
        handleClose();
        return;
    }
    if (!keepAlive)
    {
        handleClose();
        return;
    }

    // outBuffer 发送完毕
    // 关闭 EPOLLOUT。
    // 因为 socket 大多数时候一直处于可写状态，
    // 如果一直监听 EPOLLOUT，
    // epoll 会不断返回 EPOLLOUT。
    // 这样 CPU 会不断空转。
    channel->setevent(EPOLLIN | EPOLLET);
    loop.update_channel(channel);
}

void Connection::serverRequst(const HttpRequest &req)
{
    // http 响应报文的基础部分
    string statusLine;
    string headers;
    string bodyContent;

    string root = "./www";
    // 只处理get请求
    if (req.method != "GET")
    {
        statusLine = "HTTP/1.1 405 Method Not Allowed\r\n";
        bodyContent = "405 Method Not Allowed";
        headers += "content-Type: text/plain; charset=utf-8\r\n";
    }
    else
    {
        string path = req.url;
        // 去除 URL参数
        size_t q = path.find('?');
        if (q != string::npos)
            path = path.substr(0, q);

        // 默认访问 index.html
        if (path.empty() || path == "/")
            path = "/index.html";

        // 防止目录穿越
        if (path.find("..") != string::npos)
        {
            statusLine = "HTTP/1.1 403 Forbidden\r\n";
            bodyContent = "403 Forbidden";
            headers += "content-Type: text/plain; charset=utf-8\r\n";
        }
        else
        {
            string fullPath = root + path;
            if (readFile(fullPath, bodyContent))
            {
                statusLine = "HTTP/1.1 200 OK\r\n";
                headers += "Content-Type: " + mimeType(path) + "\r\n";
            }
            else
            {
                statusLine = "HTTP/1.1 404 NOT Found\r\n";
                bodyContent = "404 Not Found";
                headers += "content-Type: text/plain; charset=utf-8\r\n";
            }
        }
    }
    // 补充 Content-Length 和 Connection 头
    headers += "Content-Length: " + to_string(bodyContent.size()) + "\r\n";
    if (keepAlive)
        headers += "Connection: keep-alive\r\n";
    else
        headers += "Connection: close\r\n";

    outBuffer += statusLine + headers + "\r\n" + bodyContent;
}

void Connection::handleRead()
{
    extendLife(); // 更新倒计时
    char buf[4096];

    // 将所有数据读入 inBuffer
    while (1)
    {
        ssize_t n = recv(clientfd, buf, sizeof(buf), 0);
        if (n > 0)
        {
            inBuffer.append(buf, n);
        }
        else if (n == 0)
        {
            handleClose();
            return;
        }
        else
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            LOG_ERROR("recv error");
            handleClose();
            return;
        }
    }
    // 解析 inBuffer 中的 HTTP 报文
    while (1)
    {
        size_t consumd = 0;
        HttpRequest req;
        int r = HttpParser::parse(inBuffer, consumd, req);
        if (r == 0)
        {
            break; // 报文没读完
        }
        if (r < 0)
        {
            // 格式错误，返回 404 ，并断开
            outBuffer += "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            keepAlive = false;
            inBuffer.clear();
            break;
        }
        // 解析成功，移除inBuffer的数据
        inBuffer.erase(0, consumd);
        keepAlive = req.keepAlive;
        serverRequst(req); // 处理请求
        if (!keepAlive)    // 非长连接，断开
            break;
    }
    if (!outBuffer.empty())
    { // 如果outbuffer 有东西要发，就注册写事件
        channel->setevent(EPOLLIN | EPOLLOUT | EPOLLET);
        loop.update_channel(channel);
    }
}
void Connection::start()
{
    ptrChannel->set_close_callback([this]
                                   { handleClose(); });
    ptrChannel->set_read_callback([this]
                                  { handleRead(); });
    ptrChannel->set_write_callback([this]
                                   { handleWrite(); });
    loop.add_channel(move(ptrChannel));
    extendLife(); // 连接建立后立即注册定时器，防止静默连接永远不会被踢
}
