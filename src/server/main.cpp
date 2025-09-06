#include "chatserver.hpp"
#include "chatservice.hpp"
#include "proto_msg_handler.h"
#include <iostream>
#include <signal.h>
using namespace std;

// 处理服务器ctrl+c结束后，重置user的状态信息
void resetHandler(int)
{
    ChatService::instance()->reset();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    signal(SIGINT, resetHandler);

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    // 注册protobuf消息处理器
    ProtoMsgHandlerMap* handlerMap = ProtoMsgHandlerMap::instance();
    handlerMap->registerHandler(chat::LOGIN_MSG, std::bind(&ChatService::login, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::REG_MSG, std::bind(&ChatService::reg, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::LOGINOUT_MSG, std::bind(&ChatService::loginout, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::ONE_CHAT_MSG, std::bind(&ChatService::oneChat, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::ADD_GROUP_MSG, std::bind(&ChatService::addGroup, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    server.start();
    loop.loop();

    return 0;
}