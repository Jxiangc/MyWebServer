#include "heaptimer.h"

void HeapTimer::SwapNode_(size_t i, size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

void HeapTimer::shiftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t parent = (i - 1) / 2; // 下标从零开始，要减一
    while (i > 0) {
        if (heap_[parent] > heap_[i]) {
            SwapNode_(parent, i);
            i = parent;
            parent = (i - 1) / 2;
        } else break;
    }
}

bool HeapTimer::shiftdown_(size_t i, size_t n) {
    assert(i >= 0 && i < heap_.size());
    assert(n >= 0 && n <= heap_.size()); // 调整前 n 个元素
    auto idx = i;
    auto child = idx << 1 | 1; // 下标从零开始，左孩子
    while (child < n) {
        if (child + 1 < n && heap_[child + 1] < heap_[child]) {
            child = child + 1;
        }
        if (heap_[idx] > heap_[child]) {
            SwapNode_(idx, child);
            idx = child;
            child = idx << 1 | 1;
        } else break;
    }
    return idx > i;
}

void HeapTimer::del_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t tmp = i;
    size_t n = heap_.size() - 1;
    
    if (i < n) {
        SwapNode_(tmp, heap_.size() - 1);
        if (!shiftdown_(tmp, n)) shiftup_(tmp);
    }

    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

// 调整指定结点
void HeapTimer::adjust(int id, int newExpires) {
    assert(!heap_.empty() && ref_.count(id));
    heap_[ref_[id]].expires = Clock::now() + MS(newExpires);
    shiftdown_(ref_[id], heap_.size());
}

void HeapTimer::add(int id, int timeOut, const TimeoutCallBack& cb) {
    assert(id >= 0 && timeOut >= 0);
    if (ref_.count(id)) {
        int tmp = ref_[id];
        heap_[tmp].expires = Clock::now() + MS(timeOut);
        heap_[tmp].cb = cb;
        if (!shiftdown_(tmp, heap_.size())) shiftup_(tmp);
    } else {
        size_t n = heap_.size();
        ref_[id] = n;
        heap_.push_back({id, Clock::now() + MS(timeOut), cb});
        shiftup_(n);
    }
}

void HeapTimer::doWork(int id) {
    if (heap_.empty() || !ref_.count(id)) return;
    size_t i = ref_[id];
    auto& node = heap_[i];
    node.cb();
    del_(i);
}

void HeapTimer::tick() {
    while (!heap_.empty()) {
        auto& top = heap_.front();
        if (std::chrono::duration_cast<MS>(top.expires - Clock::now()).count() > 0) {
            break;
        }
        top.cb();
        pop();
    }
}

void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

int HeapTimer::GetNextTick() {
    tick();
    size_t res = -1;
    if (!heap_.empty()) {
        res = std::max(static_cast<int64_t>(0),
            std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count());
    }
    return res;
}