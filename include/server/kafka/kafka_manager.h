#ifndef KAFKA_MANAGER_H
#define KAFKA_MANAGER_H

#include "kafka_producer.h"
#include "kafka_consumer.h"
#include <string>
#include <memory>
#include <unordered_map>

// Kafka管理器类
// 这是一个Kafka客户端的管理类，采用单例模式，集中管理生产者和消费者实例
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
    // 回调函数参数: topic-消息来源主题, message-消息内容
    void setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback);
    
private:
    KafkaManager();
    ~KafkaManager();
    
    std::string brokers_;

    // 主题到生产者实例的映射表
    // 使用unique_ptr自动管理KafkaProducer的生命周期
    std::unordered_map<std::string, std::unique_ptr<KafkaProducer>> producers_;
    // 主题到消费者实例的映射表
    // 使用unique_ptr自动管理KafkaConsumer的生命周期
    std::unordered_map<std::string, std::unique_ptr<KafkaConsumer>> consumers_;
    // 消息回调函数对象，当消费者收到消息时调用
    std::function<void(const std::string& topic, const std::string& message)> messageCallback_;
};

#endif