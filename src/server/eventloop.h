#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <sys/eventfd.h>

#include "epoller.h"
#include "../http/httpconn.h"
#include "../timer/heaptimer.h"
#include "../log/log.h"

class EventLoop {
public:
    EventLoop(uint32_t connEvent_, int timeoutMS, const char* srcDir);
    ~EventLoop();

    void Loop();
    void Stop();

    /* 
     * 由 MainReactor 线程调用，跨线程安全地将新连接交给本 SubReactor。
     * 内部通过 eventfd 唤醒 epoll_wait。
     */
    void AddNewConn(int fd, sockaddr_in addr);

private:
    void HandlePendingConns_();
    void OnRead_(HttpConn* client);
    void OnWrite_(HttpConn* client);
    void OnProcess_(HttpConn* client);
    void CloseConn_(int fd);
    void ExtentTime_(HttpConn* client);
    void SendError_(int fd, const char* info);
    static int SetFdNonblock_(int fd);

    std::atomic<bool> isStop_;
    int wakeupFd_;

    struct PendingConn {
        int fd;
        sockaddr_in addr;
    };
    std::mutex pendingMtx_;
    std::vector<PendingConn> pendingConns_;

    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> conns_;
    std::unique_ptr<HeapTimer> timer_;
    
    uint32_t connEvent_;
    int timeoutMS_;
    const char* srcDir_;
};

#endif // EVENTLOOP_H