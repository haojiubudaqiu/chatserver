#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <muduo/net/TcpClient.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Timestamp.h>
#include "message.pb.h"

using namespace muduo;
using namespace muduo::net;

class ChatClient {
public:
    ChatClient(EventLoop* loop, const InetAddress& serverAddr)
        : client_(loop, serverAddr, "ChatClient"),
          loop_(loop) {
        client_.setConnectionCallback(
            std::bind(&ChatClient::onConnection, this, _1));
        client_.setMessageCallback(
            std::bind(&ChatClient::onMessage, this, _1, _2, _3));
    }

    void connect() {
        client_.connect();
    }

    void sendLoginRequest(int userId) {
        if (connection_) {
            chat::LoginRequest request;
            request.mutable_base()->set_msgid(chat::LOGIN_MSG);
            request.mutable_base()->set_time(Timestamp::now().microSecondsSinceEpoch());
            request.set_id(userId);
            request.set_password("testpass123");
            
            connection_->send(request.SerializeAsString());
        }
    }

private:
    void onConnection(const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            connection_ = conn;
            std::cout << "Connected to server" << std::endl;
        } else {
            connection_.reset();
            std::cout << "Disconnected from server" << std::endl;
        }
    }

    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
        // 处理服务器响应
        std::string message = buf->retrieveAllAsString();
        // std::cout << "Received message: " << message.size() << " bytes" << std::endl;
    }

    TcpClient client_;
    EventLoop* loop_;
    TcpConnectionPtr connection_;
};

void performanceTest(int clientCount, int messageCount) {
    std::cout << "Starting performance test with " << clientCount 
              << " clients and " << messageCount << " messages per client" << std::endl;

    EventLoop loop;
    InetAddress serverAddr("127.0.0.1", 7000); // Nginx负载均衡端口

    std::vector<std::unique_ptr<ChatClient>> clients;
    for (int i = 0; i < clientCount; ++i) {
        clients.emplace_back(std::make_unique<ChatClient>(&loop, serverAddr));
        clients.back()->connect();
    }

    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto startTime = std::chrono::high_resolution_clock::now();

    // 发送登录请求
    for (int i = 0; i < clientCount; ++i) {
        for (int j = 0; j < messageCount; ++j) {
            clients[i]->sendLoginRequest(i * 1000 + j);
        }
    }

    // 运行事件循环一段时间
    loop.runAfter(10.0, [&loop]() { loop.quit(); });
    loop.loop();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "Test completed in " << duration.count() << " ms" << std::endl;
    std::cout << "Total messages: " << clientCount * messageCount << std::endl;
    std::cout << "Messages per second: " << (clientCount * messageCount * 1000.0 / duration.count()) << std::endl;
}

int main() {
    std::cout << "Chat Server Performance Test" << std::endl;
    
    // 测试不同并发级别
    std::vector<std::pair<int, int>> testCases = {
        {10, 100},    // 10个客户端，每个发送100条消息
        {50, 50},     // 50个客户端，每个发送50条消息
        {100, 20}     // 100个客户端，每个发送20条消息
    };

    for (const auto& testCase : testCases) {
        performanceTest(testCase.first, testCase.second);
        std::this_thread::sleep_for(std::chrono::seconds(2)); // 测试间隔
    }

    return 0;
}