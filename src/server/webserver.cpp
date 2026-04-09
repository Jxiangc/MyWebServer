#include "webserver.h"

WebServer::WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger, 
        int sqlPort, const char* sqlUser, const  char* sqlPwd, 
        const char* dbName, int connPoolNum, int subReactorNum,
        bool openLog, int logLevel, int logQueSize)
    : port_(port), timeoutMS_(timeoutMS), openLinger_(OptLinger), 
      isClose_(false), nextLoop_(0), subReactorNum_(subReactorNum),
      epoller_(new Epoller())
{
    // 设置资源路径
    srcDir_ = getcwd(nullptr, 256); // 当前工作目录的绝对路径，设为nullptr会使用malloc为srcDir_申请空间
    assert(srcDir_);
    strcat(srcDir_, "/resources");
    
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;

    // 初始化数据连接池
    SqlConnPool::getInstance().init("localhost", sqlUser, sqlPwd, dbName, connPoolNum);
    
    // 初始化触发模式和监听socket
    InitEventMode_(trigMode);
    if (!InitSocket_()) isClose_ = true;
    
    if (openLog) {
        Log::Instance().init(logLevel, "./log", ".log", logQueSize);
        if (isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s", (listenEvent_ & EPOLLET ? "ET": "LT"), 
                                                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("SubReactor num: %d", subReactorNum_);
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d", connPoolNum);
        }
    }

    subLoops_.resize(subReactorNum_);
    for (int i = 0; i < subReactorNum_; i++) {
        subLoops_[i] = std::make_unique<EventLoop>(connEvent_, timeoutMS_, srcDir_);
    }
}

WebServer::~WebServer() {
    /* 停止所有 SubReactor */
    for (auto& loop : subLoops_) {
        loop->Stop();
    }
    for (auto& t : subThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::getInstance().shutdown();
}

void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; // EPOLLRDHUP用于检测对端关闭连接
    switch (trigMode) {
        case 0: /* LT + LT */
            break;
        case 1: /* LT + ET */
            connEvent_ |= EPOLLET;
            break;
        case 2: /* ET + LT */
            listenEvent_ |= EPOLLET;
            break;
        case 3: /* ET + ET (默认，性能最佳) */
        default:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start() {
    if (isClose_) {
        LOG_ERROR("Server start failed!");
        return;
    }

    /* 启动所有 SubReactor 线程 */
    subThreads_.resize(subReactorNum_);
    for (int i = 0; i < subReactorNum_; i++) {
        subThreads_[i] = std::thread([this, i]() {
            LOG_INFO("SubReactor[%d] thread started", i);
            subLoops_[i]->Loop();
        });
    }
    LOG_INFO("========== Server start ==========");

    while (!isClose_) {
        int eventCnt = epoller_->Wait(-1); // // 无事件时一直阻塞
        for (int i = 0; i < eventCnt; i++) {
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            // 处理事件
            if (fd == listenFd_) {
                DealListen_();
            } else {
                LOG_ERROR("MainReactor: unexpected event fd = %d", fd);
            }
        }
    }
}

void WebServer::DealListen_() {
    sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do {
        int fd = accept(listenFd_, (sockaddr*)&addr, &len);
        if (fd <= 0) { return; } // EAGAIN 或 error
        
        if (HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        
        /* Round-Robin 选择一个 SubReactor */
        int idx = nextLoop_++ % subReactorNum_;
        LOG_DEBUG("Accept new conn fd = %d -> SubReactor[%d]", fd, idx);

        /* 跨线程安全地交给 SubReactor */
        subLoops_[idx]->AddNewConn(fd, addr);

    } while (listenEvent_ & EPOLLET);
}

void WebServer::SendError_(int fd, const char* info) {
    assert(fd > 0);
    int ret = send(fd, info, strlen(info), 0);
    if(ret < 0) LOG_WARN("send error to client[%d] error!", fd);
    close(fd);
}

bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    if(port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port: %d error!",  port_);
        return false;
    }
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    // 优雅关闭
    {
        struct linger linger_opt = {0};
        if (openLinger_) {
            // 优雅关闭: 直到所剩数据发送完毕或超时
            linger_opt.l_onoff = 1;
            linger_opt.l_linger = 10;
        }

        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) {
            LOG_ERROR("create socket error!");
            return false;
        }

        ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt));
        if (ret < 0) {
            close(listenFd_);
            LOG_ERROR("init linger error!");
            return false;
        }

        // 地址复用，允许在同一端口上快速重启服务
        int optval = 1;
        ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
        if (ret < 0) {
            close(listenFd_);
            LOG_ERROR("set socket reuseaddr error!");
            return false;
        }
    }

    ret = bind(listenFd_, (sockaddr*)&addr, sizeof(addr));
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("bind port: %d error!", port_);
        return false;
    }

    ret = listen(listenFd_, 1024);
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("listen port: %d error!", port_);
        return false;
    }

    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        close(listenFd_);
        LOG_ERROR("add listen error!");
        return false;
    }

    SetFdNonblock(listenFd_);
    LOG_INFO("Server port: %d", port_);
    return true;
}

// 设置非阻塞
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

// void WebServer::CloseConn_(HttpConn* client) {
//     assert(client);
//     LOG_INFO("Client[%d] quit!", client->GetFd());
//     epoller_->DelFd(client->GetFd());
//     client->Close();
// }

// void WebServer::AddClient_(int fd, sockaddr_in& addr) {
//     assert(fd > 0);
//     users_[fd].init(fd, addr);
//     if (timeoutMS_ > 0) {
//         timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
//     }
//     epoller_->AddFd(fd, EPOLLIN | connEvent_);
//     SetFdNonblock(fd);
//     LOG_INFO("Client[%d] in!", users_[fd].GetFd());
// }

// void WebServer::DealRead_(HttpConn* client) {
//     assert(client);
//     ExtentTime_(client);
//     threadpool_->enqueue(std::bind(&WebServer::OnRead_, this, client));
// }

// void WebServer::DealWrite_(HttpConn* client) {
//     assert(client);
//     ExtentTime_(client);
//     threadpool_->enqueue(std::bind(&WebServer::OnWrite_, this, client));
// }

// // 重置超时时间
// void WebServer::ExtentTime_(HttpConn* client) {
//     assert(client);
//     if (timeoutMS_ > 0) timer_->adjust(client->GetFd(), timeoutMS_);
// }

// void WebServer::OnRead_(HttpConn* client) {
//     assert(client);
//     int ret = -1;
//     int readErrno = 0;
//     ret = client->read(&readErrno);
//     if (ret <= 0 && readErrno != EAGAIN) {
//         CloseConn_(client);
//         return;
//     }
//     OnProcess(client);
// }

// // 处理请求数据
// void WebServer::OnProcess(HttpConn* client) {
//     if (client->process()) {
//         // 读完事件通知内核说可写
//         // 响应成功，修改监听事件为写,等待OnWrite_()发送
//         epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
//     } else {
//         //写完事件就通知内核说可读
//         epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
//     }
// }

// void WebServer::OnWrite_(HttpConn* client) {
//     assert(client);
//     int ret = -1;
//     int writeErrno = 0;
//     ret = client->write(&writeErrno);
//     if (client->ToWriteBytes() == 0) {
//         // 传输完成
//         if (client->IsKeepAlive()) {
//             epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
//             return;
//         }
//     } else if (ret < 0) {
//         if (writeErrno == EAGAIN) {
//             // 缓冲区满
//             epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
//             return;
//         }
//     }
//     CloseConn_(client);
// }