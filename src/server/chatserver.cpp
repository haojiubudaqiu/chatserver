#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"

#include <iostream>
//函数对象绑定器
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;//参数占位符
using json = nlohmann::json;

// 初始化聊天服务器对象，构造函数
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开链接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    string buf = buffer->retrieveAllAsString();
    
    try {
        json js = json::parse(buf);
        
        // 检查必须字段
        if (!js.contains("msgid")) {
            throw json::exception("Missing 'msgid' field");
        }
        
        auto msgHandler = ChatService::instance()->getHandler(js["msgid"].get<int>());
        msgHandler(conn, js, time);
    }
    catch (const json::exception& e) {
        LOG_ERROR << "JSON parse error: " << e.what();
        
        if (conn->connected()) {
            json response;
            response["msgid"] = INVALID_MSG_ACK;
            response["errno"] = 400;
            response["errmsg"] = "Invalid JSON format: " + string(e.what());
            conn->send(response.dump());
        }
    }
    catch (const exception& e) {
        LOG_ERROR << "Message handling error: " << e.what();
    }
}