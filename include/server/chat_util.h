#ifndef CHAT_UTIL_H
#define CHAT_UTIL_H

#include <muduo/net/TcpConnection.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <arpa/inet.h>
#include <string>
#include <type_traits>
#include <cstdint>
#include "message.pb.h"

// Base64 编码（OpenSSL 实现，无依赖问题）
inline std::string base64Encode(const std::string& input) {
    if (input.empty()) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_push(b64, bio);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input.data(), input.size());
    (void)BIO_flush(b64);
    BUF_MEM* bufferPtr = nullptr;
    BIO_get_mem_ptr(b64, &bufferPtr);
    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(b64);
    return result;
}

// Base64 解码
inline std::string base64Decode(const std::string& input) {
    if (input.empty()) return "";
    BIO* bio = BIO_new_mem_buf(input.data(), input.size());
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_push(b64, bio);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    std::string result(input.size(), '\0');
    int len = BIO_read(b64, &result[0], input.size());
    BIO_free_all(b64);
    if (len <= 0) return "";
    result.resize(len);
    return result;
}

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
