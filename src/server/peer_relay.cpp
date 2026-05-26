#include "peer_relay.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <muduo/base/Logging.h>

static int createSocket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR << "Failed to create socket: " << strerror(errno);
        return -1;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    return fd;
}

static bool sendAll(int fd, const char* data, size_t len) {
    while (len > 0) {
        ssize_t n = ::write(fd, data, len);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += n;
        len -= n;
    }
    return true;
}

PeerRelay* PeerRelay::instance() {
    static PeerRelay inst;
    return &inst;
}

bool PeerRelay::init(uint16_t relayPort, const std::vector<std::string>& peerAddrs) {
    _peerAddrs = peerAddrs;
    _running = true;

    _listenFd = createSocket();
    if (_listenFd < 0) return false;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(relayPort);

    if (bind(_listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR << "PeerRelay bind failed on port " << relayPort << ": " << strerror(errno);
        close(_listenFd);
        _listenFd = -1;
        return false;
    }

    if (listen(_listenFd, 5) < 0) {
        LOG_ERROR << "PeerRelay listen failed: " << strerror(errno);
        close(_listenFd);
        _listenFd = -1;
        return false;
    }

    LOG_INFO << "PeerRelay listening on port " << relayPort;

    _listenThread = std::thread([this]() { listenLoop(); });
    return true;
}

void PeerRelay::listenLoop() {
    while (_running) {
        struct pollfd pfd;
        pfd.fd = _listenFd;
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR << "PeerRelay poll failed: " << strerror(errno);
            break;
        }
        if (ret == 0) continue;

        struct sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(_listenFd, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            LOG_ERROR << "PeerRelay accept failed: " << strerror(errno);
            break;
        }

        char clientIp[64];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
        LOG_DEBUG << "PeerRelay accepted connection from " << clientIp << ":" << ntohs(clientAddr.sin_port);

        // Handle connection inline (blocking read)
        std::thread([this, clientFd]() {
            // Read messages: [4-byte len][payload]
            char headerBuf[4];
            while (_running) {
                size_t total = 0;
                while (total < sizeof(headerBuf)) {
                    ssize_t n = read(clientFd, headerBuf + total, sizeof(headerBuf) - total);
                    if (n <= 0) {
                        close(clientFd);
                        return;
                    }
                    total += n;
                }

                uint32_t msgLen;
                memcpy(&msgLen, headerBuf, sizeof(msgLen));
                msgLen = ntohl(msgLen);

                if (msgLen == 0 || msgLen > 1024 * 1024) {
                    LOG_ERROR << "PeerRelay invalid message length: " << msgLen;
                    close(clientFd);
                    return;
                }

                std::string msg(msgLen, '\0');
                total = 0;
                while (total < msgLen) {
                    ssize_t n = read(clientFd, &msg[total], msgLen - total);
                    if (n <= 0) {
                        close(clientFd);
                        return;
                    }
                    total += n;
                }

                LOG_DEBUG << "PeerRelay received message len=" << msgLen;
                std::function<void(const std::string&)> cb;
                {
                    std::lock_guard<std::mutex> lock(_mutex);
                    cb = _messageCb;
                }
                if (cb) {
                    cb(msg);
                }
            }
            close(clientFd);
        }).detach();
    }
}

bool PeerRelay::sendMessage(const std::string& data) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_peerAddrs.empty()) {
        LOG_WARN << "PeerRelay no peers configured";
        return false;
    }

    uint32_t msgLen = htonl(data.size());
    std::string packet(reinterpret_cast<char*>(&msgLen), sizeof(msgLen));
    packet += data;

    bool sentAny = false;
    for (const auto& addr : _peerAddrs) {
        size_t colon = addr.find(':');
        if (colon == std::string::npos) continue;

        std::string host = addr.substr(0, colon);
        int port = std::stoi(addr.substr(colon + 1));

        int fd = createSocket();
        if (fd < 0) continue;

        struct sockaddr_in peerAddr;
        memset(&peerAddr, 0, sizeof(peerAddr));
        peerAddr.sin_family = AF_INET;
        peerAddr.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &peerAddr.sin_addr) <= 0) {
            close(fd);
            continue;
        }

        if (connect(fd, (struct sockaddr*)&peerAddr, sizeof(peerAddr)) < 0) {
            LOG_DEBUG << "PeerRelay connect to " << addr << " failed: " << strerror(errno);
            close(fd);
            continue;
        }

        if (sendAll(fd, packet.data(), packet.size())) {
            sentAny = true;
        } else {
            LOG_ERROR << "PeerRelay send to " << addr << " failed";
        }
        close(fd);
    }
    return sentAny;
}

void PeerRelay::setMessageCallback(std::function<void(const std::string& data)> cb) {
    std::lock_guard<std::mutex> lock(_mutex);
    _messageCb = std::move(cb);
}

void PeerRelay::stop() {
    _running = false;
    if (_listenFd >= 0) {
        close(_listenFd);
        _listenFd = -1;
    }
    if (_listenThread.joinable()) {
        _listenThread.join();
    }
}
