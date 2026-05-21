#include "async_logging.h"
#include "log_file.h"
#include <muduo/base/Timestamp.h>
#include <muduo/base/TimeZone.h>
#include <stdio.h>
#include <string.h>

AsyncLogging::AsyncLogging(const std::string& basename,
                           off_t rollSize,
                           int flushInterval)
    : flushInterval_(flushInterval),
      running_(false),
      basename_(basename),
      rollSize_(rollSize),
      thread_(std::bind(&AsyncLogging::threadFunc, this), "AsyncLogging"),
      latch_(1),
      mutex_(),
      cond_(mutex_),
      writeIndex_(0),
      newBuffer1_(new Buffer),
      newBuffer2_(new Buffer)
{
    buffers_[0].reset(new Buffer);
    buffers_[1].reset(new Buffer);
    buffers_[0]->bzero();
    buffers_[1]->bzero();
    full_[0].store(false, std::memory_order_relaxed);
    full_[1].store(false, std::memory_order_relaxed);
    newBuffer1_->bzero();
    newBuffer2_->bzero();
}

void AsyncLogging::append(const char* logline, int len)
{
    // 快速路径：无锁写入当前缓冲区
    int idx = writeIndex_.load(std::memory_order_relaxed);
    
    if (buffers_[idx]->avail() > len)
    {
        buffers_[idx]->append(logline, len);
        return;
    }

    // 慢速路径：缓冲区满了，需要切换
    muduo::MutexLockGuard lock(mutex_);
    
    // 双重检查（加锁后再次检查）
    idx = writeIndex_.load(std::memory_order_relaxed);
    if (buffers_[idx]->avail() > len)
    {
        buffers_[idx]->append(logline, len);
        return;
    }

    // 标记当前缓冲区为满
    full_[idx].store(true, std::memory_order_release);
    
    // 原子切换到另一个缓冲区
    int next = 1 - idx;
    writeIndex_.store(next, std::memory_order_release);
    
    // 如果另一个缓冲区也被标记为满（后台线程还没处理），需要等待
    // 这种情况很少发生，除非日志写入速度远大于磁盘写入速度
    while (full_[next].load(std::memory_order_acquire))
    {
        // 唤醒后台线程赶紧处理
        cond_.notify();
        // 短暂让出 CPU，让后台线程有机会运行
        // 注意：这里不能无限等待，否则业务线程会被阻塞
        // 实际应用中可以加超时或丢弃日志
    }
    
    // 清空新切换的缓冲区并写入
    buffers_[next]->reset();
    buffers_[next]->append(logline, len);
    
    // 唤醒后台线程处理满的缓冲区
    cond_.notify();
}

void AsyncLogging::stop()
{
    running_ = false;
    cond_.notify();
    thread_.join();
}

void AsyncLogging::threadFunc()
{
    assert(running_ == true);
    latch_.countDown();
    
    LogFile output(basename_, rollSize_, false);
    
    // 后台线程维护两个预备缓冲区
    BufferPtr newBuf1(new Buffer);
    BufferPtr newBuf2(new Buffer);
    newBuf1->bzero();
    newBuf2->bzero();
    
    std::vector<BufferPtr> writeBuffers;
    writeBuffers.reserve(16);
    
    while (running_)
    {
        assert(newBuf1 && newBuf1->length() == 0);
        assert(newBuf2 && newBuf2->length() == 0);
        assert(writeBuffers.empty());
        
        {
            muduo::MutexLockGuard lock(mutex_);
            
            // 如果没有满的缓冲区，等待一段时间（定时刷新）
            bool hasFull = full_[0].load(std::memory_order_acquire) || 
                          full_[1].load(std::memory_order_acquire);
            
            if (!hasFull)
            {
                cond_.waitForSeconds(flushInterval_);
            }
            
            // 收集所有满的缓冲区
            for (int i = 0; i < 2; ++i)
            {
                if (full_[i].load(std::memory_order_acquire))
                {
                    // 将满缓冲区的数据拷贝到新缓冲区
                    if (i == 0)
                    {
                        newBuf1->append(buffers_[0]->data(), buffers_[0]->length());
                        writeBuffers.push_back(std::move(newBuf1));
                        // 清空原缓冲区
                        buffers_[0]->reset();
                        full_[0].store(false, std::memory_order_release);
                    }
                    else
                    {
                        newBuf2->append(buffers_[1]->data(), buffers_[1]->length());
                        writeBuffers.push_back(std::move(newBuf2));
                        buffers_[1]->reset();
                        full_[1].store(false, std::memory_order_release);
                    }
                }
            }
        }
        
        // 批量写入磁盘（无锁操作）
        if (!writeBuffers.empty())
        {
            // 如果积压太多，丢弃中间的缓冲区（保护机制）
            if (writeBuffers.size() > 25)
            {
                char buf[256];
                snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
                         muduo::Timestamp::now().toFormattedString().c_str(),
                         writeBuffers.size() - 2);
                fputs(buf, stderr);
                output.append(buf, static_cast<int>(strlen(buf)));
                writeBuffers.erase(writeBuffers.begin() + 2, writeBuffers.end());
            }
            
            // 批量写入
            for (const auto& buf : writeBuffers)
            {
                output.append(buf->data(), buf->length());
            }
            
            // 回收缓冲区（保留最多 2 个）
            if (writeBuffers.size() > 2)
            {
                writeBuffers.resize(2);
            }
            
            // 回收预备缓冲区
            if (!newBuf1 && !writeBuffers.empty())
            {
                newBuf1 = std::move(writeBuffers.back());
                writeBuffers.pop_back();
                newBuf1->reset();
            }
            
            if (!newBuf2 && !writeBuffers.empty())
            {
                newBuf2 = std::move(writeBuffers.back());
                writeBuffers.pop_back();
                newBuf2->reset();
            }
            
            writeBuffers.clear();
            output.flush();
        }
    }
    
    // 退出前刷盘
    output.flush();
}
