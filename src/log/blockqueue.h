#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <deque>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <sys/time.h>

template<class T>
class BlockDeque{
public:
    explicit BlockDeque(size_t MaxCapacity = 1024);
    ~BlockDeque();

    void clear();
    bool empty();
    bool full();
    void close();
    size_t size();
    size_t capacity();

    T front();
    T back();

    void push_back(const T& item);
    void push_front(const T& item);

    bool pop(T& item);
    bool pop(T& item, int timeout);
    
    void flush();

private:
    std::deque<T> dq_;
    size_t capacity_;
    std::mutex mtx_;
    bool is_closed_;
    std::condition_variable condProducer_;
    std::condition_variable condCustomer_;
};

template<class T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity) : capacity_(MaxCapacity) {
    assert(MaxCapacity > 0);
    is_closed_ = false;
}

template<class T>
BlockDeque<T>::~BlockDeque() {
    close();
}

template<class T>
void BlockDeque<T>::clear() {
    std::lock_guard<std::mutex> lock(mtx_);
    dq_.clear();
}

template<class T>
bool BlockDeque<T>::empty() {
    std::lock_guard<std::mutex> lock(mtx_);
    return dq_.empty();
}

template<class T>
bool BlockDeque<T>::full() {
    std::lock_guard<std::mutex> lock(mtx_);
    return dq_.size() >= capacity_;
}

template<class T>
void BlockDeque<T>::close() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        dq_.clear();
        is_closed_ = true;
    }
    condProducer_.notify_all();
    condCustomer_.notify_all();
}

template<class T>
size_t BlockDeque<T>::size() {
    std::lock_guard<std::mutex> lock(mtx_);
    return dq_.size();
}

template<class T>
size_t BlockDeque<T>::capacity() {
    std::lock_guard<std::mutex> lock(mtx_);
    return capacity_;
}

template<class T>
T BlockDeque<T>::front() {
    std::lock_guard<std::mutex> lock(mtx_);
    return dq_.front();
}

template<class T>
T BlockDeque<T>::back() {
    std::lock_guard<std::mutex> lock(mtx_);
    return dq_.back();
}

template<class T>
void BlockDeque<T>::push_back(const T& item) {
    std::unique_lock<std::mutex> lock(mtx_);
    while (dq_.size() >= capacity_) {
        condProducer_.wait(lock);
    }
    dq_.push_back(item);
    condCustomer_.notify_one();
}

template<class T>
void BlockDeque<T>::push_front(const T& item) {
    std::unique_lock<std::mutex> lock(mtx_);
    while (dq_.size() >= capacity_) {
        condProducer_.wait(lock);
    }
    dq_.push_front(item);
    condCustomer_.notify_one();
}

template<class T>
bool BlockDeque<T>::pop(T& item) {
    std::unique_lock<std::mutex> lock(mtx_);
    while (dq_.empty()) {
        if (is_closed_) return false;
        condCustomer_.wait(lock);
    }
    item = dq_.front();
    dq_.pop_front();
    condProducer_.notify_one();
    return true;
}

template<class T>
bool BlockDeque<T>::pop(T& item, int timeout) {
    std::unique_lock<std::mutex> lock(mtx_);
    while (dq_.empty()) {
        if (is_closed_) {
            return false;
        }
        if (condCustomer_.wait_for(lock, std::chrono::seconds(timeout))
            == std::cv_status::timeout) 
            return false;
    }
    item = dq_.front();
    dq_.pop_front();
    condProducer_.notify_one();
    return true;
}

template<class T>
void BlockDeque<T>::flush() {
    std::lock_guard<std::mutex> lock(mtx_);
    condCustomer_.notify_one();
}

#endif