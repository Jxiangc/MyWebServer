#include "eventloop.h"

EventLoop::EventLoop(uint32_t connEvent, int timeoutMS, const char* srcDir)
    : isStop_(false), connEvent_(connEvent), 
      timeoutMS_(timeoutMS),srcDir_(srcDir), 
      epoller_(std::make_unique<Epoller>()),
      timer_(std::make_unique<HeapTimer>())
{
    /* 创建 eventfd 用于跨线程唤醒 */
    wakeupFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(wakeupFd_ >= 0);

    /* 将 wakeupFd 注册到本 loop 的 epoll */
    epoller_->AddFd(wakeupFd_, EPOLLIN | EPOLLET);
}

EventLoop::~EventLoop() {
    Stop();
    if (wakeupFd_ >= 0) {
        close(wakeupFd_);
        wakeupFd_ = -1;
    }
}

void EventLoop::Loop() {
    LOG_INFO("SubReactor loop started, wakeupFd = %d", wakeupFd_);
    while (!isStop_) {
        int timeMS = -1;
        if (timeoutMS_ > 0) {
            timeMS = timer_->GetNextTick();
        }

        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; i++) {
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);

            if (fd == wakeupFd_) {
                /* 被 MainReactor 唤醒，处理新连接 */
                uint64_t val;
                read(wakeupFd_, &val, sizeof(val)); // 消费 eventfd
                HandlePendingConns_();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                assert(conns_.count(fd) > 0);
                CloseConn_(fd);
            } else if (events & EPOLLIN) {
                assert(conns_.count(fd) > 0);
                OnRead_(&conns_[fd]);
            } else if (events & EPOLLOUT) {
                assert(conns_.count(fd) > 0);
                OnWrite_(&conns_[fd]);
            } else {
                LOG_ERROR("SubReactor: unexpected event on fd = %d", fd);
            }
        }
    }
    LOG_INFO("SubReactor loop stopped");
}

void EventLoop::Stop() {
    isStop_ = true;
    /* 唤醒可能阻塞在 epoll_wait 的线程 */
    uint64_t one = 1;
    write(wakeupFd_, &one, sizeof(one));
}

void EventLoop::AddNewConn(int fd, sockaddr_in addr) {
    {
        std::lock_guard<std::mutex> lock(pendingMtx_);
        pendingConns_.push_back({fd, addr});
    }
    /* 通过 eventfd 唤醒本 loop 的 epoll_wait */
    uint64_t one = 1;
    write(wakeupFd_, &one, sizeof(one));
}

void EventLoop::HandlePendingConns_() {
    std::vector<PendingConn> conns;
    {
        std::lock_guard<std::mutex> lock(pendingMtx_);
        conns.swap(pendingConns_);
    }
    for (auto& pc : conns) {
        
        SetFdNonblock_(pc.fd);

        /* 注册到本 loop 的 epoll */
        epoller_->AddFd(pc.fd, connEvent_ | EPOLLIN);

        /* 初始化 HttpConn */
        conns_[pc.fd].init(pc.fd, pc.addr);

        /* 添加定时器 */
        if (timeoutMS_ > 0) {
            int connFd = pc.fd;
            timer_->add(connFd, timeoutMS_,
                        [this, connFd]() { CloseConn_(connFd); });
        }

        LOG_DEBUG("SubReactor: new conn fd = %d", pc.fd);
    }
}

void EventLoop::OnRead_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);

    int readErrno = 0;
    ssize_t ret = client->read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client->GetFd());
        return;
    }
    OnProcess_(client);
}

void EventLoop::OnWrite_(HttpConn* client) {
    assert(client);
    ExtentTime_(client);

    int writeErrno = 0;
    ssize_t ret = client->write(&writeErrno);
    if (client->ToWriteBytes() == 0) {
        /* 传输完成 */
        if (client->IsKeepAlive()) {
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
            OnProcess_(client);
            return;
        }
    } else if (ret < 0) {
        if (writeErrno == EAGAIN) {
            /* 继续监听可写事件 */
            // epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    CloseConn_(client->GetFd());
}

void EventLoop::OnProcess_(HttpConn* client) {
    if (client->process()) {
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    }
}

void EventLoop::CloseConn_(int fd) {
    if (conns_.count(fd) == 0) {
        return;  // 可能已被关闭(定时器和事件同时触发)
    }
    LOG_DEBUG("SubReactor: close fd = %d", fd);
    epoller_->DelFd(fd);
    conns_[fd].Close();
    conns_.erase(fd);
}

void EventLoop::ExtentTime_(HttpConn* client) {
    assert(client);
    if (timeoutMS_ > 0) {
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

void EventLoop::SendError_(int fd, const char* info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0) {
        LOG_WARN("send error to fd = %d failed", fd);
    }
    close(fd);
}

int EventLoop::SetFdNonblock_(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}