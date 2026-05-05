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
    config.thread_pool_min_size = 1;
    config.thread_pool_max_size = 4;
    config.max_sessions = 10;
    config.session_timeout = std::chrono::seconds(60);

    server_ = std::make_unique<mcp::server>(config);
    server_->set_server_info("ChatClusterServer", "1.0.0");
    server_->set_instructions(
        "This MCP server provides monitoring and management tools for the cluster chat server. "
        "Use these tools to query server statistics, online users, user information, "
        "friend lists, and group details."
    );

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
            size_t connCount = svc->getConnectionCount();
            auto onlineIds = svc->getOnlineUserIds();
            return {
                {"connections", connCount},
                {"onlineUsers", onlineIds.size()},
                {"serverInfo", "Cluster Chat Server v1.0.0"}
            };
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_list_online_users")
            .with_description("List all currently online users with their IDs")
            .build(),
        [svc](const json&, const string&) -> json {
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
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_user_info")
            .with_description("Get detailed information about a specific user by their ID")
            .with_number_param("user_id", "The ID of the user to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            int userId = params["user_id"].get<int>();
            User user = svc->getUserModel().query(userId);
            if (user.getId() == -1) {
                return {{"error", "User not found"}, {"userId", userId}};
            }
            return {
                {"user", userToJson(user)},
                {"isOnline", user.getState() == "online"}
            };
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_user_friends")
            .with_description("Get the friend list of a specific user")
            .with_number_param("user_id", "The ID of the user whose friends to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            int userId = params["user_id"].get<int>();
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
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_get_group_info")
            .with_description("Get detailed information about a group including its members")
            .with_number_param("group_id", "The ID of the group to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            int groupId = params["group_id"].get<int>();
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
        }
    );

    server_->register_tool(
        mcp::tool_builder("chat_list_user_groups")
            .with_description("List all groups that a user belongs to")
            .with_number_param("user_id", "The ID of the user whose groups to query", true)
            .build(),
        [svc](const json& params, const string&) -> json {
            int userId = params["user_id"].get<int>();
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
        }
    );

    LOG_INFO << "Registered " << 6 << " MCP tools for chat server management";
}