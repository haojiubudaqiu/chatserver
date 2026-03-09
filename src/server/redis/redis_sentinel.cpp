#include "redis_sentinel.h"
#include <iostream>
#include <sstream>
#include <muduo/base/Logging.h>

RedisSentinel::RedisSentinel(const std::vector<std::string>& sentinelAddrs,
                             const std::string& masterName)
    : sentinelAddrs_(sentinelAddrs),
      masterName_(masterName),
      currentMasterHost_(""),
      currentMasterPort_(0),
      sentinelCtx_(nullptr),
      masterCtx_(nullptr),
      connected_(false),
      running_(false),
      currentSlaveIndex_(0) {
}

RedisSentinel::~RedisSentinel() {
    stopListen();
    if (sentinelCtx_) {
        redisFree(sentinelCtx_);
    }
    if (masterCtx_) {
        redisFree(masterCtx_);
    }
}

bool RedisSentinel::parseAddr(const std::string& addr, std::string& host, int& port) {
    size_t pos = addr.find(':');
    if (pos == std::string::npos) {
        return false;
    }
    host = addr.substr(0, pos);
    port = std::stoi(addr.substr(pos + 1));
    return true;
}

redisContext* RedisSentinel::connectTo(const std::string& host, int port) {
    redisContext* ctx = redisConnect(host.c_str(), port);
    if (ctx == nullptr || ctx->err) {
        if (ctx) {
            LOG_ERROR << "Redis connect error: " << ctx->errstr;
            redisFree(ctx);
        } else {
            LOG_ERROR << "Redis connect error: can't allocate context";
        }
        return nullptr;
    }
    return ctx;
}

bool RedisSentinel::connect() {
    for (const auto& addr : sentinelAddrs_) {
        std::string host;
        int port;
        if (parseAddr(addr, host, port)) {
            sentinelCtx_ = connectTo(host, port);
            if (sentinelCtx_) {
                if (getMasterAddrFromSentinel()) {
                    connected_ = true;
                    LOG_INFO << "Connected to sentinel: " << addr;
                    
                    masterCtx_ = connectTo(currentMasterHost_, currentMasterPort_);
                    return true;
                }
            }
        }
    }
    LOG_ERROR << "Failed to connect to any sentinel";
    return false;
}

bool RedisSentinel::getMasterAddrFromSentinel() {
    if (!sentinelCtx_) return false;
    
    redisReply* reply = (redisReply*)redisCommand(sentinelCtx_, 
        "SENTINEL GET-MASTER-ADDR-BY-NAME %s", masterName_.c_str());
    
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY || reply->elements < 2) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    
    currentMasterHost_ = reply->element[0]->str;
    currentMasterPort_ = std::stoi(reply->element[1]->str);
    
    freeReplyObject(reply);
    
    LOG_INFO << "Master address: " << currentMasterHost_ << ":" << currentMasterPort_;
    return true;
}

std::shared_ptr<redisContext> RedisSentinel::getMasterConnection() {
    std::lock_guard<std::mutex> lock(masterMutex_);
    
    if (masterCtx_ && redisPing(masterCtx_) == REDIS_OK) {
        return std::shared_ptr<redisContext>(masterCtx_, 
            [](redisContext*) { });
    }
    
    if (getMasterAddrFromSentinel()) {
        if (masterCtx_) {
            redisFree(masterCtx_);
        }
        masterCtx_ = connectTo(currentMasterHost_, currentMasterPort_);
    }
    
    if (masterCtx_) {
        return std::shared_ptr<redisContext>(masterCtx_, 
            [](redisContext*) { });
    }
    
    return nullptr;
}

std::shared_ptr<redisContext> RedisSentinel::getSlaveConnection() {
    if (!sentinelCtx_) {
        return getMasterConnection();
    }
    
    redisReply* reply = (redisReply*)redisCommand(sentinelCtx_, 
        "SENTINEL SLAVES %s", masterName_.c_str());
    
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return getMasterConnection();
    }
    
    for (size_t i = 0; i < reply->elements; ++i) {
        redisReply* slaveInfo = reply->element[i];
        
        std::string ip, port, masterLinkStatus;
        
        for (size_t j = 0; j < slaveInfo->elements; j += 2) {
            std::string key(slaveInfo->element[j]->str);
            if (key == "ip") {
                ip = slaveInfo->element[j + 1]->str;
            } else if (key == "port") {
                port = slaveInfo->element[j + 1]->str;
            } else if (key == "master_link_status") {
                masterLinkStatus = slaveInfo->element[j + 1]->str;
            }
        }
        
        if (!ip.empty() && !port.empty() && masterLinkStatus == "up") {
            freeReplyObject(reply);
            
            redisContext* ctx = connectTo(ip, std::stoi(port));
            if (ctx) {
                return std::shared_ptr<redisContext>(ctx, 
                    [](redisContext*) { });
            }
        }
    }
    
    freeReplyObject(reply);
    return getMasterConnection();
}

void RedisSentinel::setFailoverHandler(std::function<void(const std::string&, int)> handler) {
    failoverHandler_ = handler;
}

void RedisSentinel::startListen() {
    if (running_) return;
    
    running_ = true;
    listenThread_ = std::thread([this]() {
        listenSentinel();
    });
}

void RedisSentinel::stopListen() {
    running_ = false;
    if (listenThread_.joinable()) {
        listenThread_.join();
    }
}

void RedisSentinel::listenSentinel() {
    if (!sentinelCtx_) return;
    
    redisAppendCommand(sentinelCtx_, "SUBSCRIBE +switch-master +sdown +odown");
    
    while (running_) {
        redisReply* reply = nullptr;
        if (REDIS_OK == redisGetReply(sentinelCtx_, (void**)&reply)) {
            if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
                std::string msgType(reply->element[0]->str);
                std::string channel(reply->element[1]->str);
                std::string message(reply->element[2]->str);
                
                LOG_INFO << "Sentinel message: " << channel << " - " << message;
                
                if (channel == "+switch-master") {
                    std::istringstream iss(message);
                    std::string oldMaster, oldPort, newMaster, newPort;
                    iss >> oldMaster >> oldPort >> newMaster >> newPort;
                    
                    currentMasterHost_ = newMaster;
                    currentMasterPort_ = std::stoi(newPort);
                    
                    LOG_WARN << "Redis failover! New master: " << newMaster << ":" << newPort;
                    
                    if (failoverHandler_) {
                        failoverHandler_(newMaster, std::stoi(newPort));
                    }
                    
                    {
                        std::lock_guard<std::mutex> lock(masterMutex_);
                        if (masterCtx_) {
                            redisFree(masterCtx_);
                        }
                        masterCtx_ = connectTo(newMaster, std::stoi(newPort));
                    }
                }
            }
            if (reply) freeReplyObject(reply);
        }
    }
}

void RedisSentinel::reconnect() {
    if (sentinelCtx_) {
        redisFree(sentinelCtx_);
    }
    
    for (const auto& addr : sentinelAddrs_) {
        std::string host;
        int port;
        if (parseAddr(addr, host, port)) {
            sentinelCtx_ = connectTo(host, port);
            if (sentinelCtx_) {
                if (getMasterAddrFromSentinel()) {
                    connected_ = true;
                    
                    std::lock_guard<std::mutex> lock(masterMutex_);
                    if (masterCtx_) {
                        redisFree(masterCtx_);
                    }
                    masterCtx_ = connectTo(currentMasterHost_, currentMasterPort_);
                    
                    LOG_INFO << "Reconnected to sentinel";
                    return;
                }
            }
        }
    }
    
    connected_ = false;
    LOG_ERROR << "Failed to reconnect to sentinel cluster";
}
