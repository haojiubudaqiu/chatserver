#include "kafka_manager.h"
#include <muduo/base/Logging.h>

// 构造函数体为空，所有初始化工作在成员初始化列表中完成
KafkaManager::KafkaManager() {}

KafkaManager::~KafkaManager() {
    // 清理所有生产者和消费者
    // 使用clear()方法清空映射表，unique_ptr会自动释放管理的对象
    producers_.clear();// 清空生产者映射表，释放所有KafkaProducer实例
    consumers_.clear();// 清空消费者映射表，释放所有KafkaConsumer实例
}

KafkaManager* KafkaManager::instance() {
    static KafkaManager instance;
    return &instance;
}

// 初始化Kafka管理器
bool KafkaManager::init(const std::string& brokers) {
    // 存储Kafka集群地址
    brokers_ = brokers;
    LOG_INFO << "KafkaManager initialized with brokers: " << brokers;
    // 返回初始化成功
    // 注意：这里只是存储了brokers地址，没有真正测试连接
    // 实际的生产者和消费者会在第一次使用时才创建和初始化
    return true;
}

// 获取指定主题的生产者实例
KafkaProducer* KafkaManager::getProducer(const std::string& topic) {
    // 检查是否已存在该主题的生产者
    // 使用find方法在producers_映射表中查找指定主题
    auto it = producers_.find(topic);


    if (it != producers_.end()) {
        // 使用unique_ptr的get()方法获取原始指针
        return it->second.get();
    }
    
    // 创建新的生产者
    // 使用make_unique创建KafkaProducer实例，并传入brokers_和topic参数
    std::unique_ptr<KafkaProducer> producer = std::make_unique<KafkaProducer>(brokers_, topic);
    // 初始化生产者
    if (!producer->init()) {
        LOG_ERROR << "Failed to initialize Kafka producer for topic: " << topic;
        return nullptr;
    }
    // 保存生产者指针（用于返回）
    KafkaProducer* producerPtr = producer.get();
    // 将生产者移动到映射表中
    // 使用std::move将所有权转移给映射表
    producers_[topic] = std::move(producer);
    return producerPtr;
}

// 获取指定主题的消费者实例
KafkaConsumer* KafkaManager::getConsumer(const std::string& topic) {
    // 检查是否已存在该主题的消费者
    auto it = consumers_.find(topic);
    if (it != consumers_.end()) {
        // 返回找到的消费者指针
        // 使用unique_ptr的get()方法获取原始指针
        return it->second.get();
    }
    
    // 创建新的消费者
    // 使用make_unique创建KafkaConsumer实例，并传入brokers_和topic参数
    std::unique_ptr<KafkaConsumer> consumer = std::make_unique<KafkaConsumer>(brokers_, topic);
    
    if (!consumer->init()) {
        LOG_ERROR << "Failed to initialize Kafka consumer for topic: " << topic;
        return nullptr;
    }
    
    KafkaConsumer* consumerPtr = consumer.get();
    consumers_[topic] = std::move(consumer);
    return consumerPtr;
}

// 发送消息到指定主题
bool KafkaManager::sendMessage(const std::string& topic, const std::string& message) {
    // 获取指定主题的生产者实例
    // 如果生产者不存在，会自动创建
    KafkaProducer* producer = getProducer(topic);
    if (producer == nullptr) {
        LOG_ERROR << "Failed to get Kafka producer for topic: " << topic;
        return false;
    }
    
    return producer->sendMessage(topic, message);
}

// 设置消息回调函数（用于消费者）
void KafkaManager::setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback) {
    // 存储回调函数
    messageCallback_ = callback;
    
    // 为所有现有的消费者设置回调函数
    // 遍历consumers_映射表中的所有键值对
    for (auto& pair : consumers_) {
        // 为每个消费者设置回调函数
        pair.second->setMessageCallback(callback);
    }
}