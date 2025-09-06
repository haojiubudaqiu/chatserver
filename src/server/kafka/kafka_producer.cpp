#include "kafka_producer.h"
#include <iostream>
#include <muduo/base/Logging.h>

// 条件编译：如果定义了HAS_LIBRDKAFKA宏，则包含librdkafka头文件
#ifdef HAS_LIBRDKAFKA
#include <librdkafka/rdkafka.h> // 包含Kafka客户端库的主要头文件
#endif

KafkaProducer::KafkaProducer(const std::string& brokers, const std::string& topic) 
    : brokers_(brokers), topic_(topic), producer_(nullptr) {}

KafkaProducer::~KafkaProducer() {
#ifdef HAS_LIBRDKAFKA // 只有在有librdkafka库时才执行清理
    if (producer_ != nullptr) {
        // 释放Kafka生产者资源
        rd_kafka_destroy(static_cast<rd_kafka_t*>(producer_));
    }
#endif
}

// 初始化Kafka生产者
bool KafkaProducer::init() {
#ifndef HAS_LIBRDKAFKA// 如果没有librdkafka库
    LOG_WARN << "librdkafka not available, Kafka producer not initialized";
    return false;
#else
    char errstr[512];// 错误信息缓冲区
    // 创建新的Kafka配置对象
    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    
    // 设置broker列表
    if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers_.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        // 记录配置失败错误日志
        LOG_ERROR << "Failed to configure Kafka producer: " << errstr;
        // 释放配置对象
        rd_kafka_conf_destroy(conf);
        return false;
    }
    
    // 创建producer实例
    producer_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    
    if (producer_ == nullptr) {
        LOG_ERROR << "Failed to create Kafka producer: " << errstr;
        rd_kafka_conf_destroy(conf);
        return false;
    }
    
    LOG_INFO << "Kafka producer initialized successfully";
    return true;
#endif
}

// 发送消息到默认主题（重载函数）
bool KafkaProducer::sendMessage(const std::string& message) {
     // 调用另一个重载函数，使用默认主题
    return sendMessage(topic_, message);
}

// 发送消息到指定主题
bool KafkaProducer::sendMessage(const std::string& topic, const std::string& message) {
#ifndef HAS_LIBRDKAFKA
    LOG_WARN << "librdkafka not available, cannot send Kafka message";
    return false;
#else
    // 检查生产者是否已初始化
    if (producer_ == nullptr) {
        LOG_ERROR << "Kafka producer not initialized";
        return false;
    }

    // 创建Kafka主题对象
    rd_kafka_topic_t *rkt = rd_kafka_topic_new(static_cast<rd_kafka_t*>(producer_), topic.c_str(), nullptr);
    if (rkt == nullptr) {
        LOG_ERROR << "Failed to create Kafka topic: " << topic;
        return false;
    }

    // 设置分区策略：使用Kafka自动分区（RD_KAFKA_PARTITION_UA）
    int32_t partition = RD_KAFKA_PARTITION_UA;  // 让Kafka自动选择分区

    // 生产消息到Kafka
    int err = rd_kafka_produce(rkt, partition, RD_KAFKA_MSG_F_COPY,
                               const_cast<char*>(message.c_str()), message.length(),
                               nullptr, 0, nullptr);
    
    if (err) {
        // 记录生产消息失败错误日志
        LOG_ERROR << "Failed to produce Kafka message: " << rd_kafka_err2str(rd_kafka_last_error());
        // 释放主题对象
        rd_kafka_topic_destroy(rkt);
        return false;
    }
    
    // 等待消息发送完成
    rd_kafka_flush(static_cast<rd_kafka_t*>(producer_), 1000);

    // 释放主题对象
    rd_kafka_topic_destroy(rkt);
    
    LOG_INFO << "Message sent to Kafka topic: " << topic;
    return true;
#endif
}