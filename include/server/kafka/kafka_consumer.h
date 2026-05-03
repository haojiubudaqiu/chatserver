#ifndef KAFKA_CONSUMER_H
#define KAFKA_CONSUMER_H

#include <string>
#include <functional>
#include <memory>

// Kafka消费者类（占位符，实际实现需要librdkafka库）
// 这是一个Kafka消息队列消费者的C++封装类
class KafkaConsumer {
public:
    // brokers: Kafka集群地址
    // topic: 默认订阅主题
    // groupId: 消费者组ID（每个服务器使用不同的groupId实现广播）
    KafkaConsumer(const std::string& brokers, const std::string& topic, const std::string& groupId = "chat_server_group");
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
    std::string brokers_;  // Kafka集群地址
    std::string topic_;    // 默认订阅主题
    std::string groupId_;  // 消费者组ID
    void* consumer_;       // rd_kafka_t*类型
    std::function<void(const std::string& topic, const std::string& message)> messageCallback_;
    bool running_;         // 运行标志
};

#endif