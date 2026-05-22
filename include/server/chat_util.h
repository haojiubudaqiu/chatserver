#ifndef CHAT_UTIL_H
#define CHAT_UTIL_H

#include <muduo/net/TcpConnection.h>
#include <arpa/inet.h>
#include <string>
#include <type_traits>
#include "message.pb.h"

inline void sendMsg(const muduo::net::TcpConnectionPtr& conn, int32_t msgid, const std::string& serializedMsg) {
    if (!conn) return;
    int32_t len = 4 + serializedMsg.size();
    int32_t net_len = htonl(len);
    int32_t net_msgid = htonl(msgid);
    
    muduo::net::Buffer buffer;
    buffer.append(&net_len, 4);
    buffer.append(&net_msgid, 4);
    buffer.append(serializedMsg.data(), serializedMsg.size());
    
    conn->send(&buffer);
}

// Helper with SFINAE for C++17
template <typename T, typename = void>
struct HasBase : std::false_type {};

template <typename T>
struct HasBase<T, std::void_t<decltype(std::declval<T>().base())>> : std::true_type {};

template <typename T>
inline void sendProtoMsg(const muduo::net::TcpConnectionPtr& conn, const T& msg) {
    int32_t msgid = chat::INVALID_MSG;
    
    if constexpr (HasBase<T>::value) {
        msgid = msg.base().msgid();
    } else {
        msgid = msg.msgid();
    }
    
    sendMsg(conn, msgid, msg.SerializeAsString());
}

#endif
