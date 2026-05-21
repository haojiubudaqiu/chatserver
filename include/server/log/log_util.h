#ifndef LOG_UTIL_H
#define LOG_UTIL_H

#include <muduo/base/Logging.h>
#include "async_logging.h"
#include <memory>

// 异步日志全局实例
extern std::unique_ptr<AsyncLogging> g_asyncLog;

// 异步日志输出回调函数
void asyncLogOutput(const char* msg, int len);

// 异步日志刷新回调函数
void asyncLogFlush();

// 初始化异步日志系统
void initAsyncLogging(const std::string& basename = "ChatServer",
                      off_t rollSize = 100 * 1024 * 1024,  // 100MB
                      int flushInterval = 3);               // 3秒刷新

// 停止异步日志系统
void stopAsyncLogging();

#endif
