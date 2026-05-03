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
    // brokers: Kafka地址
    // groupId: 消费者组ID（每个服务器使用不同的groupId实现广播）
    bool init(const std::string& brokers, const std::string& groupId = "chat_server_group");
    
    // 获取生产者实例
    KafkaProducer* getProducer(const std::string& topic);
    
    // 获取消费者实例
    KafkaConsumer* getConsumer(const std::string& topic);
    
    // 发送消息
    bool sendMessage(const std::string& topic, const std::string& message);
    
    // 设置消息回调函数
    // 回调函数参数: topic-消息来源主题, message-消息内容
    void setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback);
    
    // 初始化并启动消费者线程
    // topics: 要订阅的主题列表
    void initConsumers(const std::vector<std::string>& topics);
    
    // 停止所有消费者
    void stopConsumers();
    
private:
    KafkaManager();
    ~KafkaManager();
    
    std::string brokers_;
    std::string groupId_;  // 消费者组ID

    // 主题到生产者实例的映射表
    std::unordered_map<std::string, std::unique_ptr<KafkaProducer>> producers_;
    // 主题到消费者实例的映射表
    std::unordered_map<std::string, std::unique_ptr<KafkaConsumer>> consumers_;
    // 消息回调函数对象，当消费者收到消息时调用
    std::function<void(const std::string& topic, const std::string& message)> messageCallback_;
    // 消费者线程列表
    std::vector<std::thread> consumerThreads_;
};

#endif