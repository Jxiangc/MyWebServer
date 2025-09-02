#include "buffer.h"

size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}

size_t Buffer::WriteableBytes() const {
    return buffer_.size() - writePos_;
}

size_t Buffer::PrependableBytes() const {
    return readPos_;
}

// 查看下一个要读取的数据
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

// 确保可写长度
void Buffer::EnsureWriteable(size_t len) {
    if (len > WriteableBytes()) {
        MakeSpace_(len);
    }
    assert(len <= WriteableBytes());
}

// 移动写下标，在Append()中使用
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
}

// 取出len长度的数据
void Buffer::Retrieve(size_t len) {
    readPos_ += len;
}

// 读取到end的前一位置
void Buffer::RetrieveUntil(const char* end) {
    assert(end >= Peek());
    Retrieve(end - Peek());
}

// 取出所有数据
void Buffer::RetrieveAll() {
    bzero(&buffer_, buffer_.size());
    readPos_ = writePos_ = 0;
}

// 取出剩余数据并返回可读字符串
std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

// 写指针位置
const char* Buffer::BeginWriteConst() const {
    return &buffer_[writePos_];
}

// 写指针
char* Buffer::BeginWrite() {
    return &buffer_[writePos_];
}

void Buffer::Append(const std::string& str) {
    Append(str.c_str(), str.size());
}

void Buffer::Append(const void* data, size_t len) {
    Append(static_cast<const char*>(data), len);
}

// 添加数据到缓冲区
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    EnsureWriteable(len);
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

// 将fd的数据读取到缓冲区
ssize_t Buffer::ReadFd(int fd, int* Errno) {
    char buff[65535];
    struct iovec iov[2];
    size_t writeable = WriteableBytes();

    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writeable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    int len = readv(fd, iov, 2);
    if (len < 0) {
        *Errno = errno;
    } else if (static_cast<size_t>(len) <= writeable) {
        HasWritten(len);
    } else {
        writePos_ = buffer_.size();
        Append(buff, static_cast<size_t>(len - writeable));
    }
    return len;
}

// 将缓冲区中的数据写入fd
ssize_t Buffer::WriteFd(int fd, int *Errno) {
    ssize_t len = write(fd, Peek(), ReadableBytes());
    if (len < 0) {
        *Errno = errno;
        return len;
    }
    Retrieve(len);
    return len;
}

// buffer开头
char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

// 扩展空间
void Buffer::MakeSpace_(size_t len) {
    if (len > WriteableBytes() + PrependableBytes()) {
        buffer_.resize(writePos_ + len + 1);
    } else {
        size_t readable = ReadableBytes();
        std::copy(buffer_.begin() + readPos_, buffer_.begin() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readable;
        assert(readable == ReadableBytes());
    }
}
