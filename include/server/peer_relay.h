#ifndef PEER_RELAY_H
#define PEER_RELAY_H

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <functional>

class PeerRelay {
public:
    static PeerRelay* instance();

    bool init(uint16_t relayPort, const std::vector<std::string>& peerAddrs);
    void setMessageCallback(std::function<void(const std::string& data)> cb);
    bool sendMessage(const std::string& data);
    void stop();

private:
    PeerRelay() = default;
    ~PeerRelay() { stop(); }

    void listenLoop();

    int _listenFd = -1;
    std::vector<std::string> _peerAddrs;
    std::function<void(const std::string& data)> _messageCb;
    std::mutex _mutex;
    std::thread _listenThread;
    bool _running = false;
};

#endif