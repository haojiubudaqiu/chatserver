#include "client_proto.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <thread>

using namespace std;

int connect_server(const char* ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "connect failed" << endl;
        return -1;
    }
    return fd;
}

string recv_all(int fd, int timeout_ms = 5000) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    
    string result;
    char buf[65536];
    
    while (true) {
        fd_set read_fds = fds;
        int ret = select(fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret <= 0) break;
        
        int n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        result.append(buf, n);
        
        // Reset timeout for subsequent reads
        tv.tv_sec = 1;
        tv.tv_usec = 0;
    }
    return result;
}

int main(int argc, char* argv[]) {
    const char* ip = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 6000;
    int fromid = argc > 3 ? atoi(argv[3]) : 27;
    int toid = argc > 4 ? atoi(argv[4]) : 28;
    const char* pass = argc > 5 ? argv[5] : "pass123";
    const char* toid_pass = argc > 6 ? argv[6] : pass;
    
    cout << "Testing chat flow: " << fromid << " -> " << toid << endl;
    
    // Step 1: Login fromid
    cout << "\n=== Step 1: Login user " << fromid << " ===" << endl;
    int fd = connect_server(ip, port);
    if (fd < 0) return 1;
    
    string login_req = ClientProto::createLoginRequest(fromid, pass);
    send(fd, login_req.c_str(), login_req.size(), 0);
    
    string resp = recv_all(fd, 3000);
    cout << "Response size: " << resp.size() << " bytes" << endl;
    if (resp.size() >= 8) {
        int32_t bodyLen = (unsigned char)resp[0] << 24 | (unsigned char)resp[1] << 16 |
                          (unsigned char)resp[2] << 8 | (unsigned char)resp[3];
        int32_t msgid = (unsigned char)resp[4] << 24 | (unsigned char)resp[5] << 16 |
                        (unsigned char)resp[6] << 8 | (unsigned char)resp[7];
        cout << "Login response: msgid=" << msgid << " bodyLen=" << bodyLen << endl;
        
        // Parse login response
        string payload = resp.substr(8, bodyLen - 4);
        chat::LoginResponse loginResp;
        if (loginResp.ParseFromString(payload)) {
            cout << "err_num=" << loginResp.err_num() << " errmsg=" << loginResp.errmsg() << endl;
            if (loginResp.err_num() != 0) {
                cerr << "Login failed!" << endl;
                close(fd);
                return 1;
            }
            cout << "Login success! User: " << loginResp.user().name() << endl;
        }
    }
    
    // Step 2: Send one chat message
    cout << "\n=== Step 2: Send chat message to " << toid << " ===" << endl;
    int64_t now = time(nullptr);
    string chat_req = ClientProto::createOneChatMessage(fromid, toid, "Hello from test!", now);
    send(fd, chat_req.c_str(), chat_req.size(), 0);
    cout << "Sent " << chat_req.size() << " bytes" << endl;
    
    // Wait a bit for server to process
    this_thread::sleep_for(chrono::milliseconds(500));
    
    // Step 3: Logout
    cout << "\n=== Step 3: Logout ===" << endl;
    string logout_req = ClientProto::createLogoutRequest(fromid);
    send(fd, logout_req.c_str(), logout_req.size(), 0);
    
    // Wait for any responses
    resp = recv_all(fd, 2000);
    cout << "Remaining response: " << resp.size() << " bytes" << endl;
    
    close(fd);
    cout << "\n=== DONE ===" << endl;
    
    // Step 4: Login toid and check offline messages
    cout << "\n=== Step 4: Login user " << toid << " to check offline messages ===" << endl;
    fd = connect_server(ip, port);
    if (fd < 0) return 1;
    
    string login_req2 = ClientProto::createLoginRequest(toid, toid_pass);
    send(fd, login_req2.c_str(), login_req2.size(), 0);
    
    resp = recv_all(fd, 3000);
    cout << "Response size: " << resp.size() << " bytes" << endl;
    if (resp.size() >= 8) {
        int32_t bodyLen = (unsigned char)resp[0] << 24 | (unsigned char)resp[1] << 16 |
                          (unsigned char)resp[2] << 8 | (unsigned char)resp[3];
        int32_t msgid = (unsigned char)resp[4] << 24 | (unsigned char)resp[5] << 16 |
                        (unsigned char)resp[6] << 8 | (unsigned char)resp[7];
        cout << "Login response: msgid=" << msgid << " bodyLen=" << bodyLen << endl;
        
        string payload = resp.substr(8, bodyLen - 4);
        chat::LoginResponse loginResp;
        if (loginResp.ParseFromString(payload)) {
            cout << "err_num=" << loginResp.err_num() << " errmsg=" << loginResp.errmsg() << endl;
            if (loginResp.err_num() == 0) {
                cout << "Offline messages: " << loginResp.offlinemsg_size() << endl;
                for (int i = 0; i < loginResp.offlinemsg_size(); ++i) {
                    string msgStr = loginResp.offlinemsg(i);
                    chat::OneChatMessage chatMsg;
                    if (chatMsg.ParseFromString(msgStr)) {
                        cout << "  From " << chatMsg.base().fromid() << ": " << chatMsg.message() << endl;
                    }
                }
                if (loginResp.offlinemsg_size() > 0) {
                    cout << "SUCCESS: Offline message received!" << endl;
                } else {
                    cerr << "FAIL: No offline messages!" << endl;
                }
            }
        }
    }
    close(fd);
    
    return 0;
}
