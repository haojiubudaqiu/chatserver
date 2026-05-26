#include "async_logging.h"
#include "log_file.h"
#include <iostream>
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
    int idx = writeIndex_.load(std::memory_order_relaxed);

    if (buffers_[idx]->avail() > len)
    {
        buffers_[idx]->append(logline, len);
        return;
    }

    muduo::MutexLockGuard lock(mutex_);

    idx = writeIndex_.load(std::memory_order_relaxed);
    if (buffers_[idx]->avail() > len)
    {
        buffers_[idx]->append(logline, len);
        return;
    }

    full_[idx].store(true, std::memory_order_release);

    int next = 1 - idx;
    writeIndex_.store(next, std::memory_order_release);

    while (full_[next].load(std::memory_order_acquire))
    {
        cond_.notify();
    }

    buffers_[next]->reset();
    buffers_[next]->append(logline, len);

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

            bool hasFull = full_[0].load(std::memory_order_acquire) ||
                          full_[1].load(std::memory_order_acquire);

            if (!hasFull)
            {
                cond_.waitForSeconds(flushInterval_);
                hasFull = full_[0].load(std::memory_order_acquire) ||
                          full_[1].load(std::memory_order_acquire);
                if (!hasFull)
                {
                    int idx = writeIndex_.load(std::memory_order_acquire);
                    if (buffers_[idx]->length() > 0)
                    {
                        full_[idx].store(true, std::memory_order_release);
                        int next = 1 - idx;
                        writeIndex_.store(next, std::memory_order_release);
                        buffers_[next]->reset();
                        hasFull = true;
                    }
                }
            }

            for (int i = 0; i < 2; ++i)
            {
                if (full_[i].load(std::memory_order_acquire))
                {
                    if (i == 0)
                    {
                        newBuf1->append(buffers_[0]->data(), buffers_[0]->length());
                        writeBuffers.push_back(std::move(newBuf1));
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

        if (!writeBuffers.empty())
        {
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

            for (const auto& buf : writeBuffers)
            {
                output.append(buf->data(), buf->length());
            }

            if (writeBuffers.size() > 2)
            {
                writeBuffers.resize(2);
            }

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

    output.flush();
}
