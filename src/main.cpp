#include <csignal>
#include <cstdlib>

#include "log.h"
#include "tcpserver.h"

using namespace std;

int main()
{

    // 忽略 SIGPIPE 信号，防止客户端异常断开导致服务器崩溃
    signal(SIGPIPE, SIG_IGN);

    // 初始化单例模式的日志系统，日志路径为当前目录下的 server.log
    Log::instance().init("server.log");
    LOG_INFO("High Performance Web Server start !");

    // 启动 TcpServer
    TcpServer server(8080);
    server.start();

    Log::instance().stop();
}
