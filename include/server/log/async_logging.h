#ifndef ASYNC_LOGGING_H
#define ASYNC_LOGGING_H

#include <muduo/base/Thread.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Condition.h>
#include <muduo/base/LogStream.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AsyncLogging : muduo::noncopyable
{
public:
    AsyncLogging(const std::string& basename,
                 off_t rollSize,
                 int flushInterval = 3);

    ~AsyncLogging()
    {
        if (running_)
        {
            stop();
        }
    }

    void append(const char* logline, int len);

    void start()
    {
        running_ = true;
        thread_.start();
        latch_.wait();
    }

    void stop();

private:
    void threadFunc();

    typedef muduo::detail::FixedBuffer<muduo::detail::kLargeBuffer> Buffer;
    typedef std::unique_ptr<Buffer> BufferPtr;

    const int flushInterval_;
    std::atomic<bool> running_;
    std::string basename_;
    off_t rollSize_;
    
    muduo::Thread thread_;
    muduo::CountDownLatch latch_;
    muduo::MutexLock mutex_;
    muduo::Condition cond_;
    
    BufferPtr buffers_[2];          // 双缓冲
    std::atomic<int> writeIndex_;   // 原子索引：当前写入哪个缓冲区 (0 或 1)
    std::atomic<bool> full_[2];     // 标记缓冲区是否已满，等待后台处理
    
    BufferPtr newBuffer1_;          // 后台线程预备的缓冲区
    BufferPtr newBuffer2_;
};

#endif
