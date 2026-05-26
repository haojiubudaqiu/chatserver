#include "chatserver.hpp"
#include "chatservice.hpp"
#include "message.pb.h"
#include "proto_msg_handler.h"

#include <functional>
#include <string>
#include "muduo/base/Logging.h"
using namespace std;
using namespace placeholders;

ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));
    _server.setThreadNum(4);
}

void ChatServer::start()
{
    _server.start();
}

void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    while (buffer->readableBytes() >= 8) {
        int32_t len = buffer->peekInt32(); // Length is msgid(4) + payload_size
        if (len <= 0 || len > 1024 * 1024) { 
            LOG_ERROR << "Invalid length: " << len;
            conn->shutdown();
            break;
        }
        
        if (buffer->readableBytes() < static_cast<size_t>(len + 4)) {
            break; // Message not fully arrived yet
        }
        
        buffer->retrieve(4); // Consume Length
        int32_t msgid = buffer->readInt32(); // Consume MsgId
        string buf = buffer->retrieveAsString(len - 4); // Consume Payload
        
        auto msgHandler = ProtoMsgHandlerMap::instance()->getHandler(static_cast<chat::MsgType>(msgid));
        if (msgHandler) {
            msgHandler(conn, buf, time);
        } else {
            LOG_ERROR << "Failed to find handler for msgid: " << msgid;
        }
    }
}
