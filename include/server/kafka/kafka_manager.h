#ifndef KAFKA_MANAGER_H
#define KAFKA_MANAGER_H

#include "kafka_producer.h"
#include "kafka_consumer.h"
#include <string>
#include <memory>
#include <unordered_map>

// Kafka管理器类
class KafkaManager {
public:
    static KafkaManager* instance();
    
    // 初始化Kafka管理器
    bool init(const std::string& brokers);
    
    // 获取生产者实例
    KafkaProducer* getProducer(const std::string& topic);
    
    // 获取消费者实例
    KafkaConsumer* getConsumer(const std::string& topic);
    
    // 发送消息
    bool sendMessage(const std::string& topic, const std::string& message);
    
    // 设置消息回调函数
    void setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback);
    
private:
    KafkaManager();
    ~KafkaManager();
    
    std::string brokers_;
    std::unordered_map<std::string, std::unique_ptr<KafkaProducer>> producers_;
    std::unordered_map<std::string, std::unique_ptr<KafkaConsumer>> consumers_;
    std::function<void(const std::string& topic, const std::string& message)> messageCallback_;
};

#endif