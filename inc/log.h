#ifndef LOG_H
#define LOG_H

#include <queue>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace std;

class Log
{
private:
    queue<string> queue_;         // 阻塞队列，缓存日志
    mutex mtx;                    // 互斥锁
    condition_variable cv;        // 条件变量，队列为空时让后台线程睡觉，有数据时唤醒
    thread thread_;               // 后台写日志的线程
    atomic<bool> running_{false}; // 记录 日志系统是否还在运行
    ofstream ofs;                 // 文件输出流

    Log() {}; // 单例模式，私有化构造函数

    // 后台工作的死循环
    void work();

public:
    ~Log() {};
    static Log &instance();
    // 初始化，打开日志文件并启动后台线程
    void init(const string &filename);

    // 停止日志系统，等待残留的日志写完
    void stop();

    // 前端线程调用这个接口提交日志
    void log(const string &level, const string &msg);
};

#define LOG_INFO(msg)                            \
    do                                           \
    {                                            \
        ostringstream oss;                       \
        oss << msg;                              \
        Log::instance().log("INFO ", oss.str()); \
    } while (0)
#define LOG_ERROR(msg)                           \
    do                                           \
    {                                            \
        ostringstream oss;                       \
        oss << msg;                              \
        Log::instance().log("ERROR", oss.str()); \
    } while (0)

#endif
