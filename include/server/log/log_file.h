#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>
#include <boost/noncopyable.hpp>
#include <boost/scoped_ptr.hpp>

namespace muduo
{
namespace FileUtil
{
class AppendFile;
}
}

class LogFile : boost::noncopyable
{
public:
    LogFile(const std::string& basename,
            size_t rollSize,
            bool threadSafe = true,
            int flushInterval = 3,
            int checkEveryN = 1024);
    ~LogFile();
    
    void append(const char* logline, int len);
    void flush();
    bool rollFile();

private:
    void append_unlocked(const char* logline, int len);
    
    static std::string getLogFileName(const std::string& basename, time_t* now);
    
    const std::string basename_;
    const size_t rollSize_;
    const int flushInterval_;
    const int checkEveryN_;
    
    int count_;
    
    boost::scoped_ptr<muduo::MutexLock> mutex_;
    time_t startOfPeriod_;
    time_t lastRoll_;
    time_t lastFlush_;
    boost::scoped_ptr<muduo::FileUtil::AppendFile> file_;
    
    const static int kRollPerSeconds_ = 60*60*24;
};

#endif