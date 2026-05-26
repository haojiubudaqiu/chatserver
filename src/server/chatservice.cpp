#include "chat_util.h"
#include "chatservice.hpp"
#include "message.pb.h"
#include "cache_manager.h"
#include "peer_relay.h"
#include <muduo/base/Logging.h>
#include <vector>
#include <cstdlib>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;//一个单例对象
    return &service;
}

ChatService::ChatService()
{   
    std::string redisSentinel1 = getenv("REDIS_SENTINEL1") ? getenv("REDIS_SENTINEL1") : "";
    std::string redisSentinel2 = getenv("REDIS_SENTINEL2") ? getenv("REDIS_SENTINEL2") : "";
    std::string redisSentinel3 = getenv("REDIS_SENTINEL3") ? getenv("REDIS_SENTINEL3") : "";
    bool hasSentinel = !redisSentinel1.empty() && !redisSentinel2.empty() && !redisSentinel3.empty();
    
    if (hasSentinel) {
        std::vector<std::string> sentinelAddrs = {redisSentinel1, redisSentinel2, redisSentinel3};
        if (!CacheManager::instance()->initWithSentinel(sentinelAddrs, "mymaster")) {
            CacheManager::instance()->init();
        }
    } else {
        LOG_INFO << "No sentinel configured, using direct Redis connection";
        CacheManager::instance()->init();
    }
    
    _kafkaManager = KafkaManager::instance();
    
    const char* kafkaHost = getenv("KAFKA_HOST") ? getenv("KAFKA_HOST") : "localhost";
    const char* kafkaPort = getenv("KAFKA_PORT") ? getenv("KAFKA_PORT") : "9092";
    std::string kafkaBroker = std::string(kafkaHost) + ":" + kafkaPort;
    
    char* serverPort = getenv("SERVER_PORT");
    std::string groupId = serverPort ? 
        std::string("chat_server_group_") + serverPort : 
        std::string("chat_server_group_default");
    _kafkaManager->init(kafkaBroker, groupId);
    
    _kafkaManager->setMessageCallback(std::bind(&ChatService::handleKafkaMessage, this, _1, _2));
    
    std::vector<std::string> topics = {"user_messages", "group_messages"};
    _kafkaManager->initConsumers(topics);
    
    // Initialize PeerRelay for cross-server TCP relay
    char* relayPortStr = getenv("RELAY_PORT");
    char* relayPeersStr = getenv("RELAY_PEERS");
    if (relayPortStr && relayPeersStr) {
        uint16_t relayPort = static_cast<uint16_t>(atoi(relayPortStr));
        std::vector<std::string> peerAddrs;
        std::string peers(relayPeersStr);
        size_t pos = 0;
        while ((pos = peers.find(',')) != std::string::npos) {
            peerAddrs.push_back(peers.substr(0, pos));
            peers.erase(0, pos + 1);
        }
        if (!peers.empty()) peerAddrs.push_back(peers);
        
        if (PeerRelay::instance()->init(relayPort, peerAddrs)) {
            PeerRelay::instance()->setMessageCallback([this](const std::string& data) {
                if (data.size() < 1) return;
                uint8_t type = static_cast<uint8_t>(data[0]);
                std::string protoData = data.substr(1);
                std::string topic = (type == 0x02) ? "group_messages" : "user_messages";
                handleKafkaMessage(topic, protoData);
            });
            LOG_INFO << "PeerRelay initialized on port " << relayPort;
        }
    } else {
        LOG_INFO << "RELAY_PORT/RELAY_PEERS not set, peer relay disabled";
    }
}

//对类的方法进行实现

// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把online状态的用户，设置成offline
    _userModel.resetState();
}


// 处理登录业务  id  pwd   pwd
void ChatService::login(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::LoginRequest loginReq;
    if (!loginReq.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse LoginRequest message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int id = loginReq.id();
    const string& pwd = loginReq.password();

    // 登录时强制读主库，避免主从延迟导致登录失败
    User user = _userModel.query(id, true);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登录
            chat::LoginResponse response;
            response.mutable_base()->set_msgid(chat::LOGIN_MSG_ACK);
            response.mutable_base()->set_time(time.microSecondsSinceEpoch());
            response.set_err_num(2);
            response.set_errmsg("this account is using, input another!");
            sendProtoMsg(conn, response);
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // 登录成功，更新用户状态信息 state offline=>online
            user.setState("online");
            if (!_userModel.updateState(user)) {
                LOG_ERROR << "Failed to update user state to online for user: " << id;
            }
            CacheManager::instance()->cacheUser(user);
            CacheManager::instance()->cacheUserStatus(user.getId(), "online");

            chat::LoginResponse response;
            response.mutable_base()->set_msgid(chat::LOGIN_MSG_ACK);
            response.mutable_base()->set_time(time.microSecondsSinceEpoch());
            response.set_err_num(0);
            response.set_errmsg("");
            response.mutable_user()->set_id(user.getId());
            response.mutable_user()->set_name(user.getName());
            response.mutable_user()->set_password(user.getPwd());
            response.mutable_user()->set_state(user.getState());
            
            // 查询该用户是否有离线消息（Base64 编码以兼容 protobuf 7.x bytes 校验）
            vector<string> vec = _offlineMsgModel.query(id);
            for (const string& msg : vec)
            {
                response.add_offlinemsg(base64Encode(msg));
            }
            // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
            if (!_offlineMsgModel.remove(id)) {
                LOG_ERROR << "Failed to remove offline messages for user: " << id;
            }

            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            for (const User &friendUser : userVec)
            {
                chat::User* userProto = response.add_friends();
                userProto->set_id(friendUser.getId());
                userProto->set_name(friendUser.getName());
                userProto->set_password(friendUser.getPwd());
                userProto->set_state(friendUser.getState());
            }

            // 查询用户的群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            for (const Group &group : groupuserVec)
            {
                chat::GroupInfo* groupInfo = response.add_groups();
                groupInfo->set_id(group.getId());
                groupInfo->set_groupname(group.getName());
                groupInfo->set_groupdesc(group.getDesc());
                
                // 添加群组用户信息
                for (const GroupUser &groupUser : group.getUsers())
                {
                    chat::GroupUser* groupUserProto = groupInfo->add_users();
                    groupUserProto->set_id(groupUser.getId());
                    groupUserProto->set_name(groupUser.getName());
                    groupUserProto->set_state(groupUser.getState());
                    groupUserProto->set_role(groupUser.getRole());
                }
            }

            sendProtoMsg(conn, response);
        }
    }
    else
    {
        // 该用户不存在，用户存在但是密码错误，登录失败
        chat::LoginResponse response;
        response.mutable_base()->set_msgid(chat::LOGIN_MSG_ACK);
        response.mutable_base()->set_time(time.microSecondsSinceEpoch());
        response.set_err_num(1);
        response.set_errmsg("id or password is invalid!");
        sendProtoMsg(conn, response);
    }
}


// 处理注册业务  name  password
void ChatService::reg(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 检查连接是否有效
    if (!conn->connected()) {
        LOG_WARN << "Connection closed during registration";
        return;
    }
    
    // 解析Protobuf消息
    chat::RegisterRequest regReq;
    if (!regReq.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse RegisterRequest message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }
    
    const string& name = regReq.name();
    const string& pwd = regReq.password();
    
    // 验证输入长度
    if (name.empty() || name.length() > 50 || pwd.empty() || pwd.length() > 50) {
        LOG_ERROR << "Invalid name or password length";
        
        chat::RegisterResponse response;
        response.mutable_base()->set_msgid(chat::REG_MSG_ACK);
        response.mutable_base()->set_time(time.microSecondsSinceEpoch());
        response.set_err_num(401);
        response.set_errmsg("Invalid name or password length");
        sendProtoMsg(conn, response);
        return;
    }
    
    User user;
    user.setName(name);
    user.setPwd(pwd);
    
    try {
        bool state = _userModel.insert(user);
        if (state) {
            // 注册成功
            chat::RegisterResponse response;
            response.mutable_base()->set_msgid(chat::REG_MSG_ACK);
            response.mutable_base()->set_time(time.microSecondsSinceEpoch());
            response.set_err_num(0);
            response.set_errmsg("");
            response.mutable_user()->set_id(user.getId());
            response.mutable_user()->set_name(user.getName());
            response.mutable_user()->set_password(user.getPwd());
            response.mutable_user()->set_state("offline");
            sendProtoMsg(conn, response);
        } else {
            // 注册失败
            chat::RegisterResponse response;
            response.mutable_base()->set_msgid(chat::REG_MSG_ACK);
            response.mutable_base()->set_time(time.microSecondsSinceEpoch());
            response.set_err_num(1);
            response.set_errmsg("Registration failed");
            sendProtoMsg(conn, response);
        }
    }
    catch (const exception& e) {
        LOG_ERROR << "Database error during registration: " << e.what();
        
        chat::RegisterResponse response;
        response.mutable_base()->set_msgid(chat::REG_MSG_ACK);
        response.mutable_base()->set_time(time.microSecondsSinceEpoch());
        response.set_err_num(500);
        response.set_errmsg("Internal server error");
        sendProtoMsg(conn, response);
    }
}


// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::LogoutRequest logoutReq;
    if (!logoutReq.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse LogoutRequest message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int userid = logoutReq.base().fromid();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    if (!_userModel.updateState(user)) {
        LOG_ERROR << "Failed to update user state to offline in loginout for user: " << userid;
    }

    // 清除 Redis 缓存
    CacheManager::instance()->invalidateUserStatus(userid);
    CacheManager::instance()->invalidateUser(userid);
}


// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it)
        {
            if (it->second == conn)
            {
                // 从map表删除用户的链接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 更新用户状态为离线
    if (user.getId() != -1)
    {
        user.setState("offline");
        if (!_userModel.updateState(user)) {
            LOG_ERROR << "Failed to update user state to offline for user: " << user.getId();
        }

        // 清除 Redis 缓存
        CacheManager::instance()->invalidateUserStatus(user.getId());
        CacheManager::instance()->invalidateUser(user.getId());
    }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::OneChatMessage chatMsg;
    if (!chatMsg.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse OneChatMessage message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int toid = chatMsg.base().toid();
    string serializedMsg = chatMsg.SerializeAsString();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            sendMsg(it->second, chatMsg.base().msgid(), serializedMsg);
            return;
        }
    }

    // Try PeerRelay TCP relay for cross-server delivery
    string relayMsg;
    relayMsg.push_back(static_cast<char>(0x01)); // topic type: user_messages
    relayMsg += serializedMsg;
    bool relaySent = PeerRelay::instance()->sendMessage(relayMsg);

    // 查询toid是否在线 (使用Redis缓存，比MySQL主从复制更快)
    string state = CacheManager::instance()->getUserStatus(toid);
    if (state == "online")
    {
        // 用户在线但不在本服务器
        if (!relaySent && _kafkaManager) {
            _kafkaManager->sendMessage("user_messages", serializedMsg);
        }
        return;
    }

    // toid不在线，存储离线消息
    if (!_offlineMsgModel.insert(toid, serializedMsg)) {
        LOG_ERROR << "Failed to store offline message for user: " << toid;
    }
}

// 添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::AddFriendRequest addFriendReq;
    if (!addFriendReq.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse AddFriendRequest message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int userid = addFriendReq.base().fromid();
    int friendid = addFriendReq.friendid();

    bool success = _friendModel.insert(userid, friendid);
    
    chat::AddFriendResponse response;
    response.mutable_base()->set_msgid(chat::ADD_FRIEND_MSG_ACK);
    response.mutable_base()->set_time(time.microSecondsSinceEpoch());
    response.set_err_num(success ? 0 : 1);
    response.set_errmsg(success ? "" : "Failed to add friend");
    sendProtoMsg(conn, response);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::CreateGroupRequest createGroupReq;
    if (!createGroupReq.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse CreateGroupRequest message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int userid = createGroupReq.base().fromid();
    const string& name = createGroupReq.groupname();
    const string& desc = createGroupReq.groupdesc();

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    bool success = _groupModel.createGroup(group);
    if (success)
    {
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
    
    chat::CreateGroupResponse response;
    response.mutable_base()->set_msgid(chat::CREATE_GROUP_MSG_ACK);
    response.mutable_base()->set_time(time.microSecondsSinceEpoch());
    response.set_err_num(success ? 0 : 1);
    response.set_errmsg(success ? "" : "Failed to create group");
    if (success) response.set_groupid(group.getId());
    sendProtoMsg(conn, response);
}

// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::AddGroupRequest addGroupReq;
    if (!addGroupReq.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse AddGroupRequest message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int userid = addGroupReq.base().fromid();
    int groupid = addGroupReq.groupid();
    bool success = _groupModel.addGroup(userid, groupid, "normal");
    
    chat::AddGroupResponse response;
    response.mutable_base()->set_msgid(chat::ADD_GROUP_MSG_ACK);
    response.mutable_base()->set_time(time.microSecondsSinceEpoch());
    response.set_err_num(success ? 0 : 1);
    response.set_errmsg(success ? "" : "Failed to join group");
    sendProtoMsg(conn, response);
}

void ChatService::groupChat(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    chat::GroupChatMessage groupChatMsg;
    if (!groupChatMsg.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse GroupChatMessage message";
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        sendProtoMsg(conn, errorMsg);
        return;
    }

    int fromid = groupChatMsg.base().fromid();
    int groupid = groupChatMsg.groupid();
    string serializedMsg = groupChatMsg.SerializeAsString();

    vector<int> useridVec = _groupModel.queryGroupUsers(fromid, groupid);
    vector<int> nonLocalUsers;
    
    {
        lock_guard<mutex> lock(_connMutex);
        for (int id : useridVec)
        {
            auto it = _userConnMap.find(id);
            if (it != _userConnMap.end())
            {
                sendMsg(it->second, groupChatMsg.base().msgid(), serializedMsg);
            }
            else
            {
                nonLocalUsers.push_back(id);
            }
        }
    }

    for (int id : nonLocalUsers)
    {
        // 优先查 Redis 缓存（比 MySQL 快），减少主从延迟问题
        string state = CacheManager::instance()->getUserStatus(id);
        if (state.empty()) {
            User user = _userModel.query(id);
            state = user.getState();
        }
        if (state != "online")
        {
            if (!_offlineMsgModel.insert(id, serializedMsg)) {
                LOG_ERROR << "Failed to store offline group msg for user: " << id;
            }
        }
    }

    // Broadcast via PeerRelay TCP relay for cross-server delivery
    string relayMsg;
    relayMsg.push_back(static_cast<char>(0x02)); // topic type: group_messages
    relayMsg += serializedMsg;
    if (!PeerRelay::instance()->sendMessage(relayMsg)) {
        // Fallback to Kafka if TCP relay fails
        if (_kafkaManager) {
            _kafkaManager->sendMessage("group_messages", serializedMsg);
        }
    }
}

// 从Kafka消息队列中获取订阅的消息
// 用于跨服务器消息传递：当一台服务器收到发给非本地用户的消息时，
// 通过 Kafka 广播到所有服务器，各服务器在本地 _userConnMap 中查找并转发
void ChatService::handleKafkaMessage(const string& topic, const string& message) {
    if (topic == "group_messages") {
        chat::GroupChatMessage groupMsg;
        if (!groupMsg.ParseFromString(message)) {
            LOG_ERROR << "Failed to parse group message from Kafka";
            return;
        }
        
        int groupid = groupMsg.groupid();
        int fromid = groupMsg.base().fromid();
        int64_t msgTime = groupMsg.base().time();
        vector<int> useridVec = _groupModel.queryGroupUsers(fromid, groupid);
        
        {
            lock_guard<mutex> lock(_connMutex);
            for (int id : useridVec) {
                auto it = _userConnMap.find(id);
                if (it != _userConnMap.end()) {
                    sendMsg(it->second, groupMsg.base().msgid(), message);
                } else {
                    // 用户可能刚断开连接，使用 SETNX 去重存储离线消息
                    string dedupKey = "group_offline:" + to_string(id) + ":"
                                    + to_string(groupid) + ":" + to_string(fromid)
                                    + ":" + to_string(msgTime);
                    if (CacheManager::instance()->setNx(dedupKey, "1", 30)) {
                        if (!_offlineMsgModel.insert(id, message)) {
                            LOG_ERROR << "Failed to store group offline msg for user: " << id;
                        }
                    }
                }
            }
        }
    } else {
        chat::OneChatMessage chatMsg;
        if (!chatMsg.ParseFromString(message)) {
            LOG_ERROR << "Failed to parse onechat message from Kafka";
            return;
        }
        
        int targetUserId = chatMsg.base().toid();
        int fromid = chatMsg.base().fromid();
        int64_t msgTime = chatMsg.base().time();
        
        {
            lock_guard<mutex> lock(_connMutex);
            auto it = _userConnMap.find(targetUserId);
            if (it != _userConnMap.end()) {
                sendMsg(it->second, chatMsg.base().msgid(), message);
            } else {
                // 用户在其他服务器断开连接但Redis中状态还未更新为offline的情况
                // 必须存储离线消息，否则消息会丢失
                // 使用 SETNX 去重，防止多台服务器重复存储
                string dedupKey = "offline:" + to_string(fromid) + ":"
                                + to_string(targetUserId) + ":" + to_string(msgTime);
                if (CacheManager::instance()->setNx(dedupKey, "1", 30)) {
                    if (!_offlineMsgModel.insert(targetUserId, message)) {
                        LOG_ERROR << "Failed to store kafka offline msg for user: " << targetUserId;
                    }
                }
            }
        }
    }
}

std::vector<int> ChatService::getOnlineUserIds() {
    std::vector<int> ids;
    lock_guard<mutex> lock(_connMutex);
    for (const auto& pair : _userConnMap) {
        ids.push_back(pair.first);
    }
    return ids;
}

size_t ChatService::getConnectionCount() {
    lock_guard<mutex> lock(_connMutex);
    return _userConnMap.size();
}

bool ChatService::sendMessageByMcp(int fromId, int toId, const string& messageContent) {
    if (messageContent.empty()) return false;
    
    chat::OneChatMessage chatMsg;
    chatMsg.mutable_base()->set_msgid(chat::ONE_CHAT_MSG);
    chatMsg.mutable_base()->set_fromid(fromId);
    chatMsg.mutable_base()->set_toid(toId);
    chatMsg.mutable_base()->set_time(muduo::Timestamp::now().microSecondsSinceEpoch());
    chatMsg.set_message(messageContent);
    string serializedMsg = chatMsg.SerializeAsString();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toId);
        if (it != _userConnMap.end()) {
            sendMsg(it->second, chatMsg.base().msgid(), serializedMsg);
            LOG_INFO << "[MCP] Sent message from user " << fromId << " to online user " << toId;
            return true;
        }
    }

    // Try TCP relay first
    {
        string relayMsg;
        relayMsg.push_back(static_cast<char>(0x01));
        relayMsg += serializedMsg;
        if (PeerRelay::instance()->sendMessage(relayMsg)) {
            LOG_INFO << "[MCP] Sent message from user " << fromId << " to user " << toId << " via PeerRelay";
            return true;
        }
    }

    // Fallback to Kafka
    if (_kafkaManager) {
        _kafkaManager->sendMessage("user_messages", serializedMsg);
        LOG_INFO << "[MCP] Published message from user " << fromId << " to user " << toId << " via Kafka";
        return true;
    }

    if (!_offlineMsgModel.insert(toId, serializedMsg)) {
        LOG_ERROR << "[MCP] Failed to store offline message for user " << toId;
        return false;
    }
    LOG_INFO << "[MCP] Stored offline message from user " << fromId << " to user " << toId;
    return true;
}