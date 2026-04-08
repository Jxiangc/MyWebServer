#include <iostream>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <future>
#include <functional>
#include <condition_variable>

class ThreadPool{
public:
    ThreadPool(size_t threadCount = 8) : stop_(false) {
        for (size_t i = 0; i < threadCount; i++) {
            workers_.emplace_back([this] {
                while (1) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->mtx_);
                        this->condition_.wait(lock, [this] {
                            return this->stop_ || !this->tasks_.empty();
                        });
                        if (this->stop_ && this->tasks_.empty())
                            return;
                        task = std::move(tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) 
            worker.join();
    }

    template<class F>
    void enqueue(F&& task) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            tasks_.emplace(std::forward<F>(task));
        }
        condition_.notify_one();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable condition_;
    bool stop_;
};