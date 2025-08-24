#ifndef KAFKA_CONSUMER_H
#define KAFKA_CONSUMER_H

#include <string>
#include <functional>
#include <memory>

// Kafka消费者类（占位符，实际实现需要librdkafka库）
class KafkaConsumer {
public:
    KafkaConsumer(const std::string& brokers, const std::string& topic);
    ~KafkaConsumer();
    
    // 初始化Kafka消费者
    bool init();
    
    // 订阅主题
    bool subscribe(const std::string& topic);
    
    // 取消订阅主题
    bool unsubscribe(const std::string& topic);
    
    // 设置消息回调函数
    void setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback);
    
    // 开始消费消息
    void startConsume();
    
    // 停止消费消息
    void stopConsume();
    
private:
    std::string brokers_;
    std::string topic_;
    void* consumer_;  // 实际应该是rd_kafka_t*类型
    std::function<void(const std::string& topic, const std::string& message)> messageCallback_;
    bool running_;
};

#endif