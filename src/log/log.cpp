#include "log.h"

Log::Log() {
    lineCount_ = 0;
    toDay_ = 0;
    isOpen_ = false;
    isAsync_ = false;
    fp_ = nullptr;
    deque_ = nullptr;
    writeThread_ = nullptr;
}

Log::~Log() {
    if (writeThread_ && writeThread_->joinable()) {
        while (!deque_->empty()) {
            deque_->flush();
        }
        deque_->close();
        writeThread_->join();
    }
    if (fp_) {
        std::lock_guard<std::mutex> locker(mtx_);
        flush();
        fclose(fp_);
    }
}

void Log::flush() {
    if (isAsync_) {
        deque_->flush();
    }
    fflush(fp_);
}

Log& Log::Instance() {
    static Log instance;
    return instance;
}

void Log::AsyncWrite_() {
    std::string str;
    while (deque_->pop(str)) {
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(str.c_str(), fp_);
    }
}

void Log::FlushLogThread() {
    Log::Instance().AsyncWrite_();
}

int Log::GetLevel() {
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    std::lock_guard<std::mutex> locker(mtx_);
    level_ = level;
}

void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
        case 0:
            buff_.Append("[debug]: ", 9);
            break;
        case 1:
            buff_.Append("[info]: ", 8);
            break;
        case 2:
            buff_.Append("[warn]: ", 8);
            break;
        case 3:
            buff_.Append("[error]: ", 9);
            break;
        default:
            buff_.Append("[debug]: ", 9);
            break;
    }
}

void Log::init(int level, const char* path,
                 const char* suffix, int maxQueueCapacity) {
    isOpen_ = true;
    level_ = level;
    path_ = path;
    suffix_ = suffix;
    if (maxQueueCapacity) {
        isAsync_ = true;
        if (!deque_) {
            // unique_ptr不支持拷贝构造和赋值操作，故使用move()触发移动构造
            std::unique_ptr<BlockDeque<std::string>> tmpQue(new BlockDeque<std::string>);
            deque_ = std::move(tmpQue);

            std::unique_ptr<std::thread> tmpThead(new std::thread(FlushLogThread));
            writeThread_ = std::move(tmpThead);
        }
    } else {
        isAsync_ = false;
    }

    lineCount_ = 0;
    time_t timer = time(nullptr);
    struct tm* systime = localtime(&timer);
    char fileName[LOG_NAME_LEN] = {0};
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
            path_, systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday, suffix_);
    toDay_ = systime->tm_mday;
    {
        std::lock_guard<std::mutex> locker(mtx_);
        buff_.RetrieveAll();
        if (fp_) {
            flush();
            fclose(fp_);
        }
        fp_ = fopen(fileName, "a");
        if (fp_ == nullptr) {
            mkdir(path_, 0777);
            fp_ = fopen(fileName, "a");
        }
        assert(fp_ != nullptr);
    }
}

void Log::write(int level, const char* format,...) {
    va_list args;
    struct timeval now;
    gettimeofday(&now, nullptr);
    time_t tSec = now.tv_sec;
    struct tm* systime = localtime(&tSec);

    if (toDay_ != systime->tm_mday || (lineCount_ && (lineCount_ % MAX_LINES == 0))) {
        char newFile[LOG_NAME_LEN];
        if (toDay_ != systime->tm_mday) {
            snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
                    path_, systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday, suffix_);
            toDay_ = systime->tm_mday;
            lineCount_ = 0;
        } else {
            snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d-%d%s",
                    path_, systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday, (lineCount_ / MAX_LINES), suffix_);
        }
        std::lock_guard<std::mutex> locker(mtx_);
        flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        lineCount_++;
        std::unique_lock<std::mutex> locker(mtx_);
        int len = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                        systime->tm_year + 1900, systime->tm_mon + 1, systime->tm_mday, 
                        systime->tm_hour, systime->tm_min, systime->tm_sec, now.tv_usec);
        
        buff_.HasWritten(len);
        AppendLogLevelTitle_(level);

        va_start(args, format);
        len = vsnprintf(buff_.BeginWrite(), buff_.WriteableBytes(), format, args);
        va_end(args);

        buff_.HasWritten(len);
        buff_.Append("\n\0", 2);

        if (isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll();
    }
}