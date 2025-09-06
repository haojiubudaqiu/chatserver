#ifndef KAFKA_PRODUCER_H
#define KAFKA_PRODUCER_H

#include <string>
#include <memory>

// Kafka生产者类（占位符，实际实现需要librdkafka库）
// 这是一个Kafka消息队列生产者的C++封装类
class KafkaProducer {
public:
    // 构造函数：创建一个Kafka生产者实例
    // brokers: Kafka集群的地址列表，格式为"host1:port1,host2:port2,..."
    KafkaProducer(const std::string& brokers, const std::string& topic);
    ~KafkaProducer();// 析构函数：清理资源，释放Kafka生产者占用的内存
    
    // 初始化Kafka生产者
    bool init();
    
    // 发送消息，发送消息到默认主题（在构造函数中指定的主题）
    bool sendMessage(const std::string& message);
    
    // 发送消息到指定主题
    bool sendMessage(const std::string& topic, const std::string& message);
    
private:
    std::string brokers_; // 存储Kafka集群地址列表
    std::string topic_;// 存储默认主题名称
    void* producer_;  // 实际应该是rd_kafka_t*类型
};

#endif