#include "log.h"

// 后台工作的死循环
void Log::work()
{
    while (true)
    {
        string line;
        {
            unique_lock<mutex> lock(mtx);
            cv.wait(lock, [this]()
                    {
 // 如果队列为空 且还在运行，就睡觉等待唤醒
 return !queue_.empty() ||!running_.load(); });
            if (queue_.empty() && (!running_.load()))
                break;
            line = move(queue_.front());
            queue_.pop();
        }
        // 写入文件
        if (ofs.is_open())
        {
            ofs << line;
            ofs.flush();
        }
        else
        {
            cout << line;
        }
    }
}

Log &Log::instance()
{
    static Log inst;
    return inst;
}

// 初始化，打开日志文件并启动后台线程
void Log::init(const string &filename)
{
    if (running_.load())
        return;
    ofs.open(filename, ios::out | ios::app);
    running_.store(true);
    thread_ = thread(&Log::work, this);
}

// 停止日志系统，等待残留的日志写完
void Log::stop()
{
    if (!running_.exchange(false))
        return;
    cv.notify_all();
    if (thread_.joinable())
        thread_.join();
    if (ofs.is_open())
        ofs.close();
}

// 前端线程调用这个接口提交日志
void Log::log(const string &level, const string &msg)
{
    // 获取时间
    time_t t = time(nullptr);           // 时间戳，从1970到现在过了多少秒
    struct tm *tm_info = localtime(&t); // 把时间戳转换成 年月日时分秒 的结构体
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info); // 从结构体格式化成字符串
    string line = string(timebuf) + "[" + level + "]" + msg + '\n';
    {
        lock_guard<mutex> lock(mtx);
        queue_.push(move(line));
    }
    cv.notify_one(); // 唤醒线程
}
