#ifndef REDIS_SENTINEL_H
#define REDIS_SENTINEL_H

#include <hiredis/hiredis.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <memory>

class RedisSentinel {
public:
    RedisSentinel(const std::vector<std::string>& sentinelAddrs,
                  const std::string& masterName = "mymaster");
    
    ~RedisSentinel();
    
    bool connect();
    
    std::shared_ptr<redisContext> getMasterConnection();
    
    std::shared_ptr<redisContext> getSlaveConnection();
    
    void reconnect();
    
    std::string getMasterHost() const { return currentMasterHost_; }
    int getMasterPort() const { return currentMasterPort_; }
    
    void setFailoverHandler(std::function<void(const std::string&, int)> handler);
    
    void startListen();
    
    void stopListen();
    
    bool isConnected() const { return connected_; }

private:
    bool getMasterAddrFromSentinel();
    
    void listenSentinel();
    
    redisContext* connectTo(const std::string& host, int port);
    
    bool parseAddr(const std::string& addr, std::string& host, int& port);
    
    std::vector<std::string> sentinelAddrs_;
    std::string masterName_;
    std::string currentMasterHost_;
    int currentMasterPort_;
    
    redisContext* sentinelCtx_;
    std::atomic<redisContext*> masterCtx_;
    std::vector<std::shared_ptr<redisContext>> slaveCtxs_;
    
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::thread listenThread_;
    
    std::mutex masterMutex_;
    std::mutex slaveMutex_;
    size_t currentSlaveIndex_;
    
    std::function<void(const std::string&, int)> failoverHandler_;
};

#endif
