#include "log_util.h"
#include <muduo/base/Logging.h>

// 全局异步日志实例
std::unique_ptr<AsyncLogging> g_asyncLog;

void asyncLogOutput(const char* msg, int len)
{
    if (g_asyncLog)
    {
        g_asyncLog->append(msg, len);
    }
}

void asyncLogFlush()
{
    // AsyncLogging 内部会自动定时刷新，这里不需要额外操作
    // 如果需要强制刷新，可以添加相应的接口
}

void initAsyncLogging(const std::string& basename,
                      off_t rollSize,
                      int flushInterval)
{
    // 创建异步日志实例
    g_asyncLog.reset(new AsyncLogging(basename, rollSize, flushInterval));
    
    // 启动异步日志线程
    g_asyncLog->start();
    
    // 重定向 muduo Logger 的输出到异步日志
    muduo::Logger::setOutput(asyncLogOutput);
    muduo::Logger::setFlush(asyncLogFlush);
    
    // 输出初始化日志（这条日志会通过异步系统写入）
    LOG_INFO << "Async logging initialized: basename=" << basename 
             << ", rollSize=" << rollSize 
             << ", flushInterval=" << flushInterval;
}

void stopAsyncLogging()
{
    if (g_asyncLog)
    {
        LOG_INFO << "Stopping async logging...";
        g_asyncLog->stop();
        g_asyncLog.reset();
    }
}
