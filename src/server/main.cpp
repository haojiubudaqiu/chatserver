#include "chatserver.hpp"
#include "chatservice.hpp"
#include "proto_msg_handler.h"
#include "mcp/chat_mcp_server.h"
#include "log_util.h"
#include <iostream>
#include <signal.h>
#include <cstring>
using namespace std;

static uint16_t g_mcpPort = 0;

void resetHandler(int)
{
    if (g_mcpPort > 0) {
        ChatMcpServer::instance()->stop();
    }
    ChatService::instance()->reset();
    stopAsyncLogging();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000 [--mcp-port 8888]" << endl;
        exit(-1);
    }

    // 初始化异步日志系统（在任何日志输出之前）
    initAsyncLogging("ChatServer", 100 * 1024 * 1024, 3);

    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--mcp-port") == 0 && i + 1 < argc) {
            g_mcpPort = atoi(argv[++i]);
        }
    }

    signal(SIGINT, resetHandler);

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    ProtoMsgHandlerMap* handlerMap = ProtoMsgHandlerMap::instance();
    handlerMap->registerHandler(chat::LOGIN_MSG, std::bind(&ChatService::login, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::REG_MSG, std::bind(&ChatService::reg, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::LOGINOUT_MSG, std::bind(&ChatService::loginout, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::ONE_CHAT_MSG, std::bind(&ChatService::oneChat, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::ADD_GROUP_MSG, std::bind(&ChatService::addGroup, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    handlerMap->registerHandler(chat::GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, ChatService::instance(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    if (g_mcpPort > 0) {
        if (ChatMcpServer::instance()->start(g_mcpPort)) {
            cout << "MCP HTTP server started on port " << g_mcpPort << endl;
        } else {
            cerr << "Warning: Failed to start MCP server on port " << g_mcpPort << endl;
        }
    }

    server.start();
    loop.loop();

    return 0;
}