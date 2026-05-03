#include "kafka_consumer.h"
#include <iostream>
#include <muduo/base/Logging.h>

#ifdef HAS_LIBRDKAFKA
#include <librdkafka/rdkafka.h>
#endif



KafkaConsumer::KafkaConsumer(const std::string& brokers, const std::string& topic, const std::string& groupId) 
    : brokers_(brokers), topic_(topic), groupId_(groupId), consumer_(nullptr), running_(false) {}

KafkaConsumer::~KafkaConsumer() {
#ifndef HAS_LIBRDKAFKA
    // Do nothing
#else // 如果有librdkafka库
    stopConsume();  // 先停止消费
    if (consumer_ != nullptr) {
        // 释放Kafka消费者资源
        rd_kafka_destroy(static_cast<rd_kafka_t*>(consumer_));
    }
#endif
}

bool KafkaConsumer::init() {
#ifndef HAS_LIBRDKAFKA
    LOG_WARN << "librdkafka not available, Kafka consumer not initialized";
    return false;
#else
    char errstr[512];// 错误信息缓冲区
    // 创建新的Kafka配置对象
    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    
    // 设置broker列表
    if (rd_kafka_conf_set(conf, "bootstrap.servers", brokers_.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        LOG_ERROR << "Failed to configure Kafka consumer: " << errstr;
        rd_kafka_conf_destroy(conf);
        return false;
    }
    
    // 设置消费者组ID（每个服务器使用不同的groupId实现广播）
    if (rd_kafka_conf_set(conf, "group.id", groupId_.c_str(), errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        LOG_ERROR << "Failed to set consumer group ID: " << errstr;
        rd_kafka_conf_destroy(conf);
        return false;
    }
    
    // 设置自动提交偏移量
    if (rd_kafka_conf_set(conf, "enable.auto.commit", "true", errstr, sizeof(errstr)) != RD_KAFKA_CONF_OK) {
        LOG_ERROR << "Failed to enable auto commit: " << errstr;
        rd_kafka_conf_destroy(conf);
        return false;
    }
    
    // 创建consumer实例
    consumer_ = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (consumer_ == nullptr) {
        LOG_ERROR << "Failed to create Kafka consumer: " << errstr;
        rd_kafka_conf_destroy(conf);
        return false;
    }
    
    // 初始化Kafka consumer轮询机制
    rd_kafka_resp_err_t err = rd_kafka_poll_set_consumer(static_cast<rd_kafka_t*>(consumer_));
    if (err) {
        LOG_ERROR << "Failed to set consumer: " << rd_kafka_err2str(err);
        return false;
    }
    
    LOG_INFO << "Kafka consumer initialized successfully";
    return true;
#endif
}


// 订阅主题
bool KafkaConsumer::subscribe(const std::string& topic) {
#ifndef HAS_LIBRDKAFKA
    LOG_WARN << "librdkafka not available, cannot subscribe to Kafka topic";
    return false;
#else
    // 检查消费者是否已初始化
    if (consumer_ == nullptr) {
        LOG_ERROR << "Kafka consumer not initialized";
        return false;
    }
    
    // 创建主题分区列表（容量为1）
    rd_kafka_topic_partition_list_t *topics = rd_kafka_topic_partition_list_new(1);
    // 添加主题到列表（使用自动分区分配）
    rd_kafka_topic_partition_list_add(topics, topic.c_str(), RD_KAFKA_PARTITION_UA);
    // 订阅主题
    rd_kafka_resp_err_t err = rd_kafka_subscribe(static_cast<rd_kafka_t*>(consumer_), topics);
    // 释放主题分区列表
    rd_kafka_topic_partition_list_destroy(topics);
    
    if (err) {
        // 记录订阅失败错误日志
        LOG_ERROR << "Failed to subscribe to topic " << topic << ": " << rd_kafka_err2str(err);
        return false;
    }
    
    LOG_INFO << "Subscribed to Kafka topic: " << topic;
    return true;
#endif
}

// 取消订阅主题
bool KafkaConsumer::unsubscribe(const std::string& topic) {
#ifndef HAS_LIBRDKAFKA
    LOG_WARN << "librdkafka not available, cannot unsubscribe from Kafka topic";
    return false;
#else
    // 检查消费者是否已初始化
    if (consumer_ == nullptr) {
        LOG_ERROR << "Kafka consumer not initialized";
        return false;
    }

    // 取消订阅所有主题
    rd_kafka_resp_err_t err = rd_kafka_unsubscribe(static_cast<rd_kafka_t*>(consumer_));
    if (err) {
        // 记录取消订阅失败错误日志
        LOG_ERROR << "Failed to unsubscribe: " << rd_kafka_err2str(err);
        return false;
    }
    // 记录取消订阅成功日志
    LOG_INFO << "Unsubscribed from Kafka topic: " << topic;
    return true;
#endif
}

// 设置消息回调函数
void KafkaConsumer::setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback) {
    messageCallback_ = callback;// 存储回调函数
}

// 开始消费消息（阻塞方法）
void KafkaConsumer::startConsume() {
#ifndef HAS_LIBRDKAFKA
    LOG_WARN << "librdkafka not available, cannot start Kafka consumer";
#else
    // 检查消费者是否已初始化
    if (consumer_ == nullptr) {
        LOG_ERROR << "Kafka consumer not initialized";
        return;
    }
    
    running_ = true;
    LOG_INFO << "Starting Kafka consumer";
    
    // 消费循环
    while (running_) {
        // 轮询消息，超时时间为1000毫秒
        rd_kafka_message_t *rkmessage = rd_kafka_consumer_poll(static_cast<rd_kafka_t*>(consumer_), 1000);
        
        if (rkmessage) {// 如果有消息
            if (rkmessage->err) {// 如果消息有错误
                if (rkmessage->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
                    // Reached end of partition
                    LOG_INFO << "Reached end of partition";
                } else if (rkmessage->err == RD_KAFKA_RESP_ERR__TIMED_OUT) {
                    // Poll timeout
                    LOG_INFO << "Poll timeout";
                } else {
                    LOG_ERROR << "Kafka consumer error: " << rd_kafka_message_errstr(rkmessage);
                }
            } else {
                // Process the message 处理正常消息
                // 从消息负载创建字符串
                std::string message(static_cast<char*>(rkmessage->payload), rkmessage->len);
                // 获取主题名称（如果可用）
                std::string topic(rkmessage->rkt ? rd_kafka_topic_name(rkmessage->rkt) : "unknown");
                // 如果有设置回调函数，则调用
                if (messageCallback_) {
                    messageCallback_(topic, message);
                }
            }
            // 销毁消息对象
            rd_kafka_message_destroy(rkmessage);
        }
    }
#endif
}

void KafkaConsumer::stopConsume() {
#ifndef HAS_LIBRDKAFKA
    LOG_WARN << "librdkafka not available, cannot stop Kafka consumer";
#else
    running_ = false;
    if (consumer_ != nullptr) {
        rd_kafka_consumer_close(static_cast<rd_kafka_t*>(consumer_));
    }
    LOG_INFO << "Kafka consumer stopped";
#endif
}