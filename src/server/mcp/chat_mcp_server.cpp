#include "chat_mcp_server.h"
#include "chatservice.hpp"
#include "mcp_server.h"
#include "mcp_tool.h"
#include "mcp_resource.h"
#include "mcp_logger.h"

#include <muduo/base/Logging.h>
#include <sstream>
#include <algorithm>

using namespace std;
using json = nlohmann::ordered_json;

ChatMcpServer* ChatMcpServer::instance() {
    static ChatMcpServer instance;
    return &instance;
}

ChatMcpServer::~ChatMcpServer() {
    stop();
}

bool ChatMcpServer::start(uint16_t port) {
    if (running_) return true;

    mcp::server::configuration config;
    config.host = "0.0.0.0";
    config.port = port;
    config.threadpool_size = 4;
    config.max_sessions = 10;
    config.session_timeout = 300;

    server_ = std::make_unique<mcp::server>(config);
    server_->set_server_info("ChatClusterServer", "1.0.0");
    server_->set_instructions(
        "This MCP server provides monitoring, management, and communication tools for the cluster chat server. "
        "You can login, query server statistics, online users, user information, friend lists, group details, "
        "and send private messages to users on behalf of a logged-in account."
    );

    server_->set_capabilities({{"tools", json::object()}});

    registerTools();

    if (!server_->start(false)) {
        LOG_ERROR << "Failed to start MCP HTTP server on port " << port;
        server_.reset();
        return false;
    }

    running_ = true;
    LOG_INFO << "MCP HTTP server started on port " << port;
    return true;
}

void ChatMcpServer::stop() {
    if (server_) {
        server_->stop();
        server_.reset();
    }
    running_ = false;
}

bool ChatMcpServer::isRunning() const {
    return running_;
}

static json userToJson(const User& user) {
    return {
        {"id", user.getId()},
        {"name", user.getName()},
        {"state", user.getState()}
    };
}

static json groupUserToJson(const GroupUser& gu) {
    return {
        {"id", gu.getId()},
        {"name", gu.getName()},
        {"state", gu.getState()},
        {"role", gu.getRole()}
    };
}

void ChatMcpServer::registerTools() {
    auto* svc = ChatService::instance();

    server_->register_tool(
        mcp::tool_builder("chat_server_stats")
            .with_description("Get cluster chat server statistics including connection count and online user count")
            .build(),
        [svc](const json&, const string&) -> json {
            try {
                size_t connCount = svc->getConnectionCount();
                auto onlineIds = svc->getOnlineUserIds();
                return {
                    {"connections", connCount},
                    {"onlineUsers", onlineIds.size()},
                    {"serverInfo", "Cluster Chat Server v1.0.0"}
                };
            } catch (const std::exception& e) {
                return {{"error", string("chat_server_stats failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_list_online_users")
            .with_description("List all currently online users with their IDs")
            .build(),
        [svc](const json&, const string&) -> json {
            try {
                auto ids = svc->getOnlineUserIds();
                json result = json::array();
                auto& userModel = svc->getUserModel();
                for (int id : ids) {
                    User user = userModel.query(id);
                    if (user.getId() != -1) {
                        result.push_back({{"id", user.getId()}, {"name", user.getName()}});
                    } else {
                        result.push_back({{"id", id}, {"name", "unknown"}});
                    }
                }
                return {{"onlineUsers", result}, {"count", ids.size()}};
            } catch (const std::exception& e) {
                return {{"error", string("chat_list_online_users failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_user_info")
            .with_description("Get detailed information about a specific user by their ID")
            .with_number_param("user_id", "The ID of the user to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int userId = params.at("user_id").get<int>();
                User user = svc->getUserModel().query(userId);
                if (user.getId() == -1) {
                    return {{"error", "User not found"}, {"userId", userId}};
                }
                return {
                    {"user", userToJson(user)},
                    {"isOnline", user.getState() == "online"}
                };
            } catch (const std::exception& e) {
                return {{"error", string("chat_get_user_info failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_user_friends")
            .with_description("Get the friend list of a specific user")
            .with_number_param("user_id", "The ID of the user whose friends to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int userId = params.at("user_id").get<int>();
                User user = svc->getUserModel().query(userId);
                if (user.getId() == -1) {
                    return {{"error", "User not found"}, {"userId", userId}};
                }
                vector<User> friends = svc->getFriendModel().query(userId);
                json friendList = json::array();
                for (const auto& f : friends) {
                    friendList.push_back(userToJson(f));
                }
                return {
                    {"userId", userId},
                    {"userName", user.getName()},
                    {"friends", friendList},
                    {"count", friends.size()}
                };
            } catch (const std::exception& e) {
                return {{"error", string("chat_get_user_friends failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_group_info")
            .with_description("Get detailed information about a group including its members")
            .with_number_param("group_id", "The ID of the group to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int groupId = params.at("group_id").get<int>();
                Group group = svc->getGroupModel().queryGroup(groupId);
                if (group.getId() == -1) {
                    return {{"error", "Group not found"}, {"groupId", groupId}};
                }
                json members = json::array();
                for (const auto& gu : group.getUsers()) {
                    members.push_back(groupUserToJson(gu));
                }
                return {
                    {"groupId", group.getId()},
                    {"groupName", group.getName()},
                    {"description", group.getDesc()},
                    {"members", members},
                    {"memberCount", group.getUsers().size()}
                };
            } catch (const std::exception& e) {
                return {{"error", string("chat_get_group_info failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_list_user_groups")
            .with_description("List all groups that a user belongs to")
            .with_number_param("user_id", "The ID of the user whose groups to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int userId = params.at("user_id").get<int>();
                User user = svc->getUserModel().query(userId);
                if (user.getId() == -1) {
                    return {{"error", "User not found"}, {"userId", userId}};
                }
                vector<Group> groups = svc->getGroupModel().queryGroups(userId);
                json groupList = json::array();
                for (const auto& g : groups) {
                    groupList.push_back({
                        {"id", g.getId()},
                        {"name", g.getName()},
                        {"desc", g.getDesc()},
                        {"memberCount", g.getUsers().size()}
                    });
                }
                return {
                    {"userId", userId},
                    {"userName", user.getName()},
                    {"groups", groupList},
                    {"count", groups.size()}
                };
            } catch (const std::exception& e) {
                return {{"error", string("chat_list_user_groups failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_user_login")
            .with_description("Authenticate a user by their ID and password. Returns user info including friends and groups on success. Use user_id parameter (numeric).")
            .with_number_param("user_id", "The numeric ID of the user to login as", true)
            .with_string_param("password", "The user's password", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int userId = params.at("user_id").get<int>();
                string password = params.at("password").get<string>();
                
                User user = svc->getUserModel().query(userId, true);
                if (user.getId() == -1) {
                    return {{"success", false}, {"error", "User not found"}, {"userId", userId}};
                }
                if (user.getPwd() != password) {
                    return {{"success", false}, {"error", "Invalid password"}, {"userId", userId}};
                }
                if (user.getState() == "online") {
                    return {{"success", false}, {"error", "User is already logged in from another device"}, {"userId", userId}, {"userName", user.getName()}};
                }
                
                vector<User> friends = svc->getFriendModel().query(userId);
                json friendsJson = json::array();
                for (const auto& f : friends) {
                    friendsJson.push_back({{"id", f.getId()}, {"name", f.getName()}, {"state", f.getState()}});
                }
                
                vector<Group> groups = svc->getGroupModel().queryGroups(userId);
                json groupsJson = json::array();
                for (const auto& g : groups) {
                    groupsJson.push_back({
                        {"id", g.getId()},
                        {"name", g.getName()},
                        {"desc", g.getDesc()},
                        {"memberCount", g.getUsers().size()}
                    });
                }
                
                return {
                    {"success", true},
                    {"message", "Login successful"},
                    {"userId", user.getId()},
                    {"userName", user.getName()},
                    {"friends", friendsJson},
                    {"friendsCount", friends.size()},
                    {"groups", groupsJson},
                    {"groupsCount", groups.size()}
                };
            } catch (const std::exception& e) {
                return {{"success", false}, {"error", string("chat_user_login failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_conversation_history")
            .with_description("Get recent conversation history between a user and the AI agent. Returns messages in chronological order. Used for memory reconstruction when AI agent restarts.")
            .with_number_param("user_id", "The user's numeric ID", true)
            .with_number_param("agent_id", "The AI agent's numeric ID (usually 10000)", true)
            .with_number_param("limit", "Number of recent messages to return (max 50, default 10)", false)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int userId = params.at("user_id").get<int>();
                int agentId = params.at("agent_id").get<int>();
                int limit = params.value("limit", 10);
                if (limit > 50) limit = 50;

                auto history = svc->getChatHistoryModel().queryPrivateChat(
                    userId, agentId, limit, 0);

                json messages = json::array();
                for (const auto& rec : history) {
                    string role = (rec.fromId == agentId) ? "assistant" : "user";
                    messages.push_back({
                        {"role", role},
                        {"content", rec.content},
                        {"time", rec.msgTime}
                    });
                }

                std::reverse(messages.begin(), messages.end());

                return {
                    {"user_id", userId},
                    {"messages", messages},
                    {"count", messages.size()}
                };
            } catch (const std::exception& e) {
                return {{"error", string("chat_get_conversation_history failed: ") + e.what()}};
            }
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_send_message")
            .with_description("Send a private message from one user to another. The sender must be authenticated first via chat_user_login.")
            .with_number_param("from_user_id", "The sender's numeric user ID", true)
            .with_number_param("to_user_id", "The recipient's numeric user ID", true)
            .with_string_param("message", "The message content to send", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            try {
                int fromId = params.at("from_user_id").get<int>();
                int toId = params.at("to_user_id").get<int>();
                string message = params.at("message").get<string>();
                
                if (message.empty()) {
                    return {{"success", false}, {"error", "Message content cannot be empty"}};
                }
                if (fromId == toId) {
                    return {{"success", false}, {"error", "Cannot send message to yourself"}};
                }
                
                User fromUser = svc->getUserModel().query(fromId);
                if (fromUser.getId() == -1) {
                    return {{"success", false}, {"error", "Sender user not found"}, {"fromUserId", fromId}};
                }
                
                User toUser = svc->getUserModel().query(toId);
                if (toUser.getId() == -1) {
                    return {{"success", false}, {"error", "Recipient user not found"}, {"toUserId", toId}};
                }
                
                bool ok = svc->sendMessageByMcp(fromId, toId, message);
                if (!ok) {
                    return {{"success", false}, {"error", "Failed to send message"}};
                }
                
                return {
                    {"success", true},
                    {"message", "Message sent successfully"},
                    {"from", {{"id", fromUser.getId()}, {"name", fromUser.getName()}}},
                    {"to", {{"id", toUser.getId()}, {"name", toUser.getName()}}},
                    {"deliveryMethod", toUser.getState() == "online" ? "direct" : "offline_stored"}
                };
            } catch (const std::exception& e) {
                return {{"success", false}, {"error", string("chat_send_message failed: ") + e.what()}};
            }
        }
    );

    LOG_INFO << "Registered " << 9 << " MCP tools for chat server management";
}