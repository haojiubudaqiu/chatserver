#include <arpa/inet.h>
#include "client_proto.h"

template<typename T>
std::string packMessage(const T& msg) {
    std::string data = msg.SerializeAsString();
    int32_t len = 4 + data.size();
    int32_t msgid = msg.base().msgid();
    
    int32_t net_len = htonl(len);
    int32_t net_msgid = htonl(msgid);
    
    std::string result;
    result.append(reinterpret_cast<char*>(&net_len), 4);
    result.append(reinterpret_cast<char*>(&net_msgid), 4);
    result.append(data);
    return result;
}

// 创建登录请求消息
std::string ClientProto::createLoginRequest(int id, const std::string& password) {
    chat::LoginRequest loginReq;
    loginReq.mutable_base()->set_msgid(chat::LOGIN_MSG);
    loginReq.mutable_base()->set_fromid(id);
    loginReq.mutable_base()->set_time(time(NULL));
    loginReq.set_id(id);
    loginReq.set_password(password);
    return packMessage(loginReq);
}

// 创建注册请求消息
std::string ClientProto::createRegisterRequest(const std::string& name, const std::string& password) {
    chat::RegisterRequest regReq;
    regReq.mutable_base()->set_msgid(chat::REG_MSG);
    regReq.mutable_base()->set_time(time(NULL));
    regReq.set_name(name);
    regReq.set_password(password);
    return packMessage(regReq);
}

// 创建一对一聊天消息
std::string ClientProto::createOneChatMessage(int fromid, int toid, const std::string& message, int64_t time) {
    chat::OneChatMessage chatMsg;
    chatMsg.mutable_base()->set_msgid(chat::ONE_CHAT_MSG);
    chatMsg.mutable_base()->set_fromid(fromid);
    chatMsg.mutable_base()->set_toid(toid);
    chatMsg.mutable_base()->set_time(time);
    chatMsg.set_message(message);
    return packMessage(chatMsg);
}

// 创建添加好友消息
std::string ClientProto::createAddFriendRequest(int fromid, int friendid) {
    chat::AddFriendRequest addFriendReq;
    addFriendReq.mutable_base()->set_msgid(chat::ADD_FRIEND_MSG);
    addFriendReq.mutable_base()->set_fromid(fromid);
    addFriendReq.mutable_base()->set_time(time(NULL));
    addFriendReq.set_friendid(friendid);
    return packMessage(addFriendReq);
}

// 创建创建群组消息
std::string ClientProto::createCreateGroupRequest(int fromid, const std::string& groupname, const std::string& groupdesc) {
    chat::CreateGroupRequest createGroupReq;
    createGroupReq.mutable_base()->set_msgid(chat::CREATE_GROUP_MSG);
    createGroupReq.mutable_base()->set_fromid(fromid);
    createGroupReq.mutable_base()->set_time(time(NULL));
    createGroupReq.set_groupname(groupname);
    createGroupReq.set_groupdesc(groupdesc);
    return packMessage(createGroupReq);
}

// 创建加入群组消息
std::string ClientProto::createAddGroupRequest(int fromid, int groupid) {
    chat::AddGroupRequest addGroupReq;
    addGroupReq.mutable_base()->set_msgid(chat::ADD_GROUP_MSG);
    addGroupReq.mutable_base()->set_fromid(fromid);
    addGroupReq.mutable_base()->set_time(time(NULL));
    addGroupReq.set_groupid(groupid);
    return packMessage(addGroupReq);
}

// 创建群聊消息
std::string ClientProto::createGroupChatMessage(int fromid, int groupid, const std::string& message, int64_t time) {
    chat::GroupChatMessage groupChatMsg;
    groupChatMsg.mutable_base()->set_msgid(chat::GROUP_CHAT_MSG);
    groupChatMsg.mutable_base()->set_fromid(fromid);
    groupChatMsg.mutable_base()->set_toid(groupid);
    groupChatMsg.mutable_base()->set_time(time);
    groupChatMsg.set_groupid(groupid);
    groupChatMsg.set_message(message);
    return packMessage(groupChatMsg);
}

// 创建注销消息
std::string ClientProto::createLogoutRequest(int fromid) {
    chat::LogoutRequest logoutReq;
    logoutReq.mutable_base()->set_msgid(chat::LOGINOUT_MSG);
    logoutReq.mutable_base()->set_fromid(fromid);
    logoutReq.mutable_base()->set_time(time(NULL));
    return packMessage(logoutReq);
}