#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <vector>
#include <algorithm>
#include <unordered_map>
#include <functional>

#include "../log/log.h"

using TimeoutCallBack = std::function<void()>;
using Clock = std::chrono::high_resolution_clock;
using TimeStamp = Clock::time_point;
using MS = std::chrono::milliseconds;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator<(const TimerNode& other) {
        return expires < other.expires;
    }
    bool operator>(const TimerNode& other) {
        return expires > other.expires;
    }
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }  // 保留（扩充）容量
    ~HeapTimer() { clear(); }
    
    void adjust(int id, int newExpires);
    void add(int id, int timeOut, const TimeoutCallBack& cb);
    void doWork(int id);
    void clear();
    void tick();
    void pop();
    int GetNextTick();

private:
    void del_(size_t i);
    void shiftup_(size_t i);
    bool shiftdown_(size_t i, size_t n);
    void SwapNode_(size_t i, size_t j);

    std::vector<TimerNode> heap_;
    // key:id value:vector的下标
    std::unordered_map<int, size_t> ref_;   // id对应的在heap_中的下标，方便用heap_的时候查找
};

#endif