#ifndef ASYNC_LOGGING_H
#define ASYNC_LOGGING_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Thread.h>
#include <muduo/base/BlockingQueue.h>
#include <muduo/base/BoundedBlockingQueue.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/LogStream.h>
#include <boost/bind.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
#include <string>
#include <vector>

class AsyncLogging : muduo::noncopyable
{
public:
    AsyncLogging(const std::string& basename,
                 size_t rollSize,
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
    
    void stop()
    {
        running_ = false;
        cond_.notify();
        thread_.join();
    }

private:
    void threadFunc();
    
    typedef muduo::detail::FixedBuffer<muduo::detail::kLargeBuffer> Buffer;
    typedef boost::shared_ptr<Buffer> BufferPtr;
    typedef boost::shared_ptr<std::vector<BufferPtr> > BufferVector;
    
    const int flushInterval_;
    bool running_;
    std::string basename_;
    size_t rollSize_;
    muduo::Thread thread_;
    muduo::CountDownLatch latch_;
    muduo::MutexLock mutex_;
    muduo::Condition cond_;
    BufferPtr currentBuffer_;
    BufferPtr nextBuffer_;
    BufferVector buffers_;
};

#endif