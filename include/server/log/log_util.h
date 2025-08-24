#ifndef LOG_UTIL_H
#define LOG_UTIL_H

#include <muduo/base/Logging.h>

// 定义日志级别
#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARN 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_FATAL 5

// 默认日志级别
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

// 日志宏定义
#define LOG_TRACE if (LOG_LEVEL <= LOG_LEVEL_TRACE) LOG_TRACE
#define LOG_DEBUG if (LOG_LEVEL <= LOG_LEVEL_DEBUG) LOG_DEBUG
#define LOG_INFO if (LOG_LEVEL <= LOG_LEVEL_INFO) LOG_INFO
#define LOG_WARN if (LOG_LEVEL <= LOG_LEVEL_WARN) LOG_WARN
#define LOG_ERROR if (LOG_LEVEL <= LOG_LEVEL_ERROR) LOG_ERROR
#define LOG_FATAL if (LOG_LEVEL <= LOG_LEVEL_FATAL) LOG_FATAL

#endif