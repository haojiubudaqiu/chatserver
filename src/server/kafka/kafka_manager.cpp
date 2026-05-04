#include "kafka_manager.h"
#include <muduo/base/Logging.h>
#include <thread>

KafkaManager::KafkaManager() {}

KafkaManager::~KafkaManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    producers_.clear();
    consumers_.clear();
}

KafkaManager* KafkaManager::instance() {
    static KafkaManager instance;
    return &instance;
}

bool KafkaManager::init(const std::string& brokers, const std::string& groupId) {
    brokers_ = brokers;
    groupId_ = groupId;
    LOG_INFO << "KafkaManager initialized with brokers: " << brokers << ", groupId: " << groupId;
    return true;
}

KafkaProducer* KafkaManager::getProducer(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = producers_.find(topic);
    if (it != producers_.end()) {
        return it->second.get();
    }
    
    std::unique_ptr<KafkaProducer> producer = std::make_unique<KafkaProducer>(brokers_, topic);
    if (!producer->init()) {
        LOG_ERROR << "Failed to initialize Kafka producer for topic: " << topic;
        return nullptr;
    }
    KafkaProducer* producerPtr = producer.get();
    producers_[topic] = std::move(producer);
    return producerPtr;
}

KafkaConsumer* KafkaManager::getConsumer(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = consumers_.find(topic);
    if (it != consumers_.end()) {
        return it->second.get();
    }
    
    std::unique_ptr<KafkaConsumer> consumer = std::make_unique<KafkaConsumer>(brokers_, topic, groupId_);
    if (!consumer->init()) {
        LOG_ERROR << "Failed to initialize Kafka consumer for topic: " << topic;
        return nullptr;
    }
    
    KafkaConsumer* consumerPtr = consumer.get();
    consumers_[topic] = std::move(consumer);
    return consumerPtr;
}

bool KafkaManager::sendMessage(const std::string& topic, const std::string& message) {
    KafkaProducer* producer = getProducer(topic);
    if (producer == nullptr) {
        LOG_ERROR << "Failed to get Kafka producer for topic: " << topic;
        return false;
    }
    return producer->sendMessage(topic, message);
}

void KafkaManager::setMessageCallback(std::function<void(const std::string& topic, const std::string& message)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    messageCallback_ = callback;
    for (auto& pair : consumers_) {
        pair.second->setMessageCallback(callback);
    }
}

void KafkaManager::initConsumers(const std::vector<std::string>& topics) {
    for (const auto& topic : topics) {
        KafkaConsumer* consumer = getConsumer(topic);
        if (consumer == nullptr) {
            LOG_ERROR << "Failed to get Kafka consumer for topic: " << topic;
            continue;
        }
        
        consumer->setMessageCallback(messageCallback_);
        
        if (!consumer->subscribe(topic)) {
            LOG_ERROR << "Failed to subscribe to Kafka topic: " << topic;
            continue;
        }
        
        consumerThreads_.emplace_back([consumer]() {
            consumer->startConsume();
        });
        
        LOG_INFO << "Started Kafka consumer for topic: " << topic;
    }
}

void KafkaManager::stopConsumers() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& consumerPair : consumers_) {
            if (consumerPair.second) {
                consumerPair.second->stopConsume();
            }
        }
    }
    
    for (auto& t : consumerThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    
    consumerThreads_.clear();
    LOG_INFO << "All Kafka consumers stopped";
}