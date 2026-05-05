#ifndef CHAT_MCP_SERVER_H
#define CHAT_MCP_SERVER_H

#include <memory>
#include <string>
#include <cstdint>

namespace mcp {
class server;
}

class ChatMcpServer {
public:
    static ChatMcpServer* instance();

    bool start(uint16_t port);
    void stop();
    bool isRunning() const;

private:
    ChatMcpServer() = default;
    ~ChatMcpServer();

    ChatMcpServer(const ChatMcpServer&) = delete;
    ChatMcpServer& operator=(const ChatMcpServer&) = delete;

    void registerTools();

    std::unique_ptr<mcp::server> server_;
    bool running_ = false;
};

#endif