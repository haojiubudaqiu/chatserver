#include "client_proto.h"

// 创建登录请求消息
std::string ClientProto::createLoginRequest(int id, const std::string& password) {
    chat::LoginRequest loginReq;
    loginReq.mutable_base()->set_msgid(chat::LOGIN_MSG);
    loginReq.mutable_base()->set_fromid(id);
    loginReq.mutable_base()->set_time(time(NULL));
    loginReq.set_id(id);
    loginReq.set_password(password);
    return loginReq.SerializeAsString();
}

// 创建注册请求消息
std::string ClientProto::createRegisterRequest(const std::string& name, const std::string& password) {
    chat::RegisterRequest regReq;
    regReq.mutable_base()->set_msgid(chat::REG_MSG);
    regReq.mutable_base()->set_time(time(NULL));
    regReq.set_name(name);
    regReq.set_password(password);
    return regReq.SerializeAsString();
}

// 创建一对一聊天消息
std::string ClientProto::createOneChatMessage(int fromid, int toid, const std::string& message, int64_t time) {
    chat::OneChatMessage chatMsg;
    chatMsg.mutable_base()->set_msgid(chat::ONE_CHAT_MSG);
    chatMsg.mutable_base()->set_fromid(fromid);
    chatMsg.mutable_base()->set_toid(toid);
    chatMsg.mutable_base()->set_time(time);
    chatMsg.set_message(message);
    return chatMsg.SerializeAsString();
}

// 创建添加好友消息
std::string ClientProto::createAddFriendRequest(int fromid, int friendid) {
    chat::AddFriendRequest addFriendReq;
    addFriendReq.mutable_base()->set_msgid(chat::ADD_FRIEND_MSG);
    addFriendReq.mutable_base()->set_fromid(fromid);
    addFriendReq.mutable_base()->set_time(time(NULL));
    addFriendReq.set_friendid(friendid);
    return addFriendReq.SerializeAsString();
}

// 创建创建群组消息
std::string ClientProto::createCreateGroupRequest(int fromid, const std::string& groupname, const std::string& groupdesc) {
    chat::CreateGroupRequest createGroupReq;
    createGroupReq.mutable_base()->set_msgid(chat::CREATE_GROUP_MSG);
    createGroupReq.mutable_base()->set_fromid(fromid);
    createGroupReq.mutable_base()->set_time(time(NULL));
    createGroupReq.set_groupname(groupname);
    createGroupReq.set_groupdesc(groupdesc);
    return createGroupReq.SerializeAsString();
}

// 创建加入群组消息
std::string ClientProto::createAddGroupRequest(int fromid, int groupid) {
    chat::AddGroupRequest addGroupReq;
    addGroupReq.mutable_base()->set_msgid(chat::ADD_GROUP_MSG);
    addGroupReq.mutable_base()->set_fromid(fromid);
    addGroupReq.mutable_base()->set_time(time(NULL));
    addGroupReq.set_groupid(groupid);
    return addGroupReq.SerializeAsString();
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
    return groupChatMsg.SerializeAsString();
}

// 创建注销消息
std::string ClientProto::createLogoutRequest(int fromid) {
    chat::LogoutRequest logoutReq;
    logoutReq.mutable_base()->set_msgid(chat::LOGINOUT_MSG);
    logoutReq.mutable_base()->set_fromid(fromid);
    logoutReq.mutable_base()->set_time(time(NULL));
    return logoutReq.SerializeAsString();
}