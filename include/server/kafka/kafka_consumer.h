#ifndef KAFKA_CONSUMER_H
#define KAFKA_CONSUMER_H

#include <string>
#include <functional>
#include <memory>

// Kafka消费者类（占位符，实际实现需要librdkafka库）
// 这是一个Kafka消息队列消费者的C++封装类
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
    std::string brokers_;// 存储Kafka集群地址列表
    std::string topic_;// 存储默认主题名称
    void* consumer_;  // 实际应该是rd_kafka_t*类型，实际实现时应使用rd_kafka_t*类型，但这里用void*保持库中立性

    // 消息回调函数对象，当收到消息时调用
    std::function<void(const std::string& topic, const std::string& message)> messageCallback_;
    bool running_;// 标志位，表示消费者是否正在运行
};

#endif