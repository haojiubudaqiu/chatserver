#include "chatservice.hpp"
#include "public.hpp"
#include "message.pb.h"
#include "cache_manager.h"
#include <muduo/base/Logging.h>
#include <vector>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;//一个单例对象
    return &service;
}

// 构造函数，注册消息以及对应的Handler回调操作
ChatService::ChatService() : _kafkaManager(nullptr), _kafkaConsumerThread(nullptr)
{   
    // 初始化缓存管理器 (Redis Sentinel模式)
    std::vector<std::string> sentinelAddrs = {
        "127.0.0.1:26379",
        "127.0.0.1:26380",
        "127.0.0.1:26381"
    };
    CacheManager::instance()->initWithSentinel(sentinelAddrs, "mymaster");
    
    // 初始化Kafka管理器
    _kafkaManager = KafkaManager::instance();
    _kafkaManager->init("localhost:9092");
    
    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
    
    // 设置Kafka消息回调
    _kafkaManager->setMessageCallback(std::bind(&ChatService::handleKafkaMessage, this, _1, _2));
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
        conn->send(errorMsg.SerializeAsString());
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
            conn->send(response.SerializeAsString());
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id); 

            // 登录成功，更新用户状态信息 state offline=>online
            user.setState("online");
            _userModel.updateState(user);

            chat::LoginResponse response;
            response.mutable_base()->set_msgid(chat::LOGIN_MSG_ACK);
            response.mutable_base()->set_time(time.microSecondsSinceEpoch());
            response.set_err_num(0);
            response.set_errmsg("");
            response.mutable_user()->set_id(user.getId());
            response.mutable_user()->set_name(user.getName());
            response.mutable_user()->set_password(user.getPwd());
            response.mutable_user()->set_state(user.getState());
            
            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            for (const string& msg : vec)
            {
                response.add_offlinemsg(msg);
            }
            // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
            _offlineMsgModel.remove(id);

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

            conn->send(response.SerializeAsString());
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
        conn->send(response.SerializeAsString());
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
        conn->send(errorMsg.SerializeAsString());
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
        conn->send(response.SerializeAsString());
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
            conn->send(response.SerializeAsString());
        } else {
            // 注册失败
            chat::RegisterResponse response;
            response.mutable_base()->set_msgid(chat::REG_MSG_ACK);
            response.mutable_base()->set_time(time.microSecondsSinceEpoch());
            response.set_err_num(1);
            response.set_errmsg("Registration failed");
            conn->send(response.SerializeAsString());
        }
    }
    catch (const exception& e) {
        LOG_ERROR << "Database error during registration: " << e.what();
        
        chat::RegisterResponse response;
        response.mutable_base()->set_msgid(chat::REG_MSG_ACK);
        response.mutable_base()->set_time(time.microSecondsSinceEpoch());
        response.set_err_num(500);
        response.set_errmsg("Internal server error");
        conn->send(response.SerializeAsString());
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
        conn->send(errorMsg.SerializeAsString());
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

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
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

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId()); 

    // 更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
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
        conn->send(errorMsg.SerializeAsString());
        return;
    }

    int fromid = chatMsg.base().fromid();
    int toid = chatMsg.base().toid();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息   服务器主动推送消息给toid用户
            it->second->send(chatMsg.SerializeAsString());
            return;
        }
    }

    // 查询toid是否在线 
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        // 使用Kafka发送消息而不是Redis
        // 直接转发protobuf消息
        if (_kafkaManager) {
            _kafkaManager->sendMessage("user_messages", chatMsg.SerializeAsString());
        }
        return;
    }

    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, chatMsg.SerializeAsString());
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
        conn->send(errorMsg.SerializeAsString());
        return;
    }

    int userid = addFriendReq.base().fromid();
    int friendid = addFriendReq.friendid();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
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
        conn->send(errorMsg.SerializeAsString());
        return;
    }

    int userid = createGroupReq.base().fromid();
    const string& name = createGroupReq.groupname();
    const string& desc = createGroupReq.groupdesc();

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group))
    {
        // 存储群组创建人信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
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
        conn->send(errorMsg.SerializeAsString());
        return;
    }

    int userid = addGroupReq.base().fromid();
    int groupid = addGroupReq.groupid();
    _groupModel.addGroup(userid, groupid, "normal");
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, const string &data, Timestamp time)
{
    // 解析Protobuf消息
    chat::GroupChatMessage groupChatMsg;
    if (!groupChatMsg.ParseFromString(data)) {
        LOG_ERROR << "Failed to parse GroupChatMessage message";
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        conn->send(errorMsg.SerializeAsString());
        return;
    }

    int fromid = groupChatMsg.base().fromid();
    int groupid = groupChatMsg.groupid();
    vector<int> useridVec = _groupModel.queryGroupUsers(fromid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec)
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(groupChatMsg.SerializeAsString());
        }
        else
        {
            // 查询toid是否在线 
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                // 使用Kafka发送消息而不是Redis
                // 直接转发protobuf消息
                if (_kafkaManager) {
                    _kafkaManager->sendMessage("group_messages", groupChatMsg.SerializeAsString());
                }
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, groupChatMsg.SerializeAsString());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}

// 从Kafka消息队列中获取订阅的消息
void ChatService::handleKafkaMessage(const string& topic, const string& message) {
    LOG_INFO << "Received Kafka message on topic: " << topic;
    
    // 直接转发protobuf消息
    lock_guard<mutex> lock(_connMutex);
    // 解析基础消息以获取目标用户ID
    chat::BaseMessage baseMsg;
    if (baseMsg.ParseFromString(message)) {
        int targetUserId = baseMsg.toid();
        auto it = _userConnMap.find(targetUserId);
        if (it != _userConnMap.end()) {
            // 用户在线，直接转发消息
            it->second->send(message);
        } else {
            // 用户不在线，存储离线消息
            _offlineMsgModel.insert(targetUserId, message);
        }
    } else {
        LOG_ERROR << "Failed to parse Kafka protobuf message";
    }
}