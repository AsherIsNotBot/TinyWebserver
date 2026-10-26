#ifndef TIMER_H
#define TIMER_H

#include <vector>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <utility>

using namespace std;

using Clock = chrono::steady_clock; // 单调时钟，不受系统时间调整影响
using TimeStamp = Clock::time_point;
using MS = chrono::milliseconds;

// 定时器节点：一个 fd 对应一个定时器
struct TimerNode
{
    int fd;
    TimeStamp expires;     // 过期绝对时间戳
    function<void()> cb;   // 超时回调函数

    bool operator<(const TimerNode &t) const
    {
        return expires < t.expires;
    }
};

// 基于小根堆的定时器容器
// 用 unordered_map 记录 fd -> 堆中下标，实现 O(log n) 的刷新与删除
// 内部自带互斥锁，允许在其它线程（如主 Reactor 投递连接时）安全访问
struct HeapTimer
{
private:
    vector<TimerNode> heap_;
    unordered_map<int, size_t> ref_; // fd -> heap_ 数组中的下标
    mutex mtx_;

    void swapNode(size_t i, size_t j)
    {
        swap(heap_[j], heap_[i]);
        ref_[heap_[i].fd] = i;
        ref_[heap_[j].fd] = j;
    }
    void siftup(size_t i)
    {
        while (i > 0)
        {
            size_t parent = (i - 1) / 2;
            if (heap_[i] < heap_[parent])
            {
                swapNode(i, parent);
                i = parent;
            }
            else
                break;
        }
    }
    bool siftdown(size_t index, size_t n)
    {
        size_t i = index;
        size_t child = i * 2 + 1;
        while (child < n)
        {
            if (child + 1 < n && heap_[child + 1] < heap_[child])
            {
                child++;
            }
            if (heap_[child] < heap_[i])
            {
                swapNode(i, child);
                i = child;
                child = i * 2 + 1;
            }
            else
                break;
        }
        return i > index;
    }
    // 删除指定下标的节点
    void del_(size_t index)
    {
        size_t n = heap_.size() - 1;
        if (index < n)
        {
            swapNode(index, n);
            if (!siftdown(index, n))
                siftup(index);
        }
        ref_.erase(heap_.back().fd);
        heap_.pop_back();
    }

public:
    // 添加定时器；如果该 fd 已存在则原地更新过期时间（惰性刷新方案）
    void add(int fd, int timeout_ms, function<void()> cb)
    {
        lock_guard<mutex> lock(mtx_);
        TimeStamp new_expires = Clock::now() + MS(timeout_ms);
        if (ref_.count(fd) == 0)
        {
            // 插入堆尾
            size_t i = heap_.size();
            ref_[fd] = i;
            heap_.push_back({fd, new_expires, move(cb)});
            siftup(i);
        }
        else
        {
            // 已有连接：原地更新过期时间
            size_t i = ref_[fd];
            heap_[i].expires = new_expires;
            if (cb)
                heap_[i].cb = move(cb);
            if (!siftdown(i, heap_.size()))
            {
                siftup(i);
            }
        }
    }

    void remove_fd(int fd)
    {
        lock_guard<mutex> lock(mtx_);
        auto it = ref_.find(fd);
        if (it != ref_.end())
            del_(it->second);
    }

    // 检查并剔除所有超时连接，返回下一个最快过期连接的剩余毫秒数（无定时器返回 -1）
    int getNextTick()
    {
        vector<function<void()> > expired_cbs;
        int next_ms = -1;
        {
            lock_guard<mutex> lock(mtx_);
            TimeStamp now = Clock::now();
            while (!heap_.empty())
            {
                const TimerNode &node = heap_.front();
                if (chrono::duration_cast<MS>(node.expires - now).count() > 0)
                    break;
                expired_cbs.push_back(node.cb);
                del_(0);
            }
            if (!heap_.empty())
            {
                auto diff = chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
                next_ms = (diff > 0) ? static_cast<int>(diff) : 0;
            }
        }
        // 回调在锁外执行，避免回调里再次访问定时器造成死锁
        for (auto &cb : expired_cbs)
        {
            if (cb)
                cb();
        }
        return next_ms;
    }
};

#endif
