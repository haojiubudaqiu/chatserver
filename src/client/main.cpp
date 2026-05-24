#include "client_proto.h"
#include <iostream>
#include <thread>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <functional>
using namespace std;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <ctime>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

User g_currentUser;
vector<User> g_currentUserFriendList;
vector<Group> g_currentUserGroupList;

bool isMainMenuRunning = false;
sem_t rwsem;
atomic_bool g_isLoginSuccess{false};

void readTaskHandler(int clientfd);
void mainMenu(int);
void showCurrentUserData();

bool waitForResponse(int timeoutSec = 10)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeoutSec;
    int ret = sem_timedwait(&rwsem, &ts);
    if (ret == -1 && errno == ETIMEDOUT)
    {
        cerr << "Error: No response from server within " << timeoutSec << " seconds" << endl;
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    sem_init(&rwsem, 0, 0);

    std::thread readTask(readTaskHandler, clientfd);
    readTask.detach();

    for (;;)
    {
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        if (cin.fail()) {
            if (cin.eof()) break;
            cin.clear();
            cin.ignore(10000, '\n');
            cerr << "无效输入，请输入数字！" << endl;
            continue;
        }
        cin.get();

        switch (choice)
        {
        case 1:
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid (数字,注册时获得的ID):";
            cin >> id;
            if (cin.fail()) {
                cin.clear();
                cin.ignore(10000, '\n');
                cerr << "请输入数字ID" << endl;
                break;
            }
            cin.get();
            cout << "userpassword:";
            cin.getline(pwd, 50);

            string request = ClientProto::createLoginRequest(id, pwd);
            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), request.size(), 0);
            if (len == -1)
            {
                cerr << "send login msg error (errno=" << errno << "): " << strerror(errno) << endl;
                break;
            }

            if (!waitForResponse()) break;
                 
            if (g_isLoginSuccess) 
            {
                isMainMenuRunning = true;
                mainMenu(clientfd);
            }
        }
        break;
        case 2:
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";
            cin.getline(pwd, 50);

            string request = ClientProto::createRegisterRequest(name, pwd);

            int len = send(clientfd, request.c_str(), request.size(), 0);
            if (len == -1)
            {
                cerr << "send reg msg error (errno=" << errno << "): " << strerror(errno) << endl;
                break;
            }
            
            if (!waitForResponse()) break;
        }
        break;
        case 3:
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

void doRegResponse(const string& responseData)
{
    chat::RegisterResponse response;
    if (!response.ParseFromString(responseData)) {
        cerr << "Failed to parse RegisterResponse" << endl;
        return;
    }
    
    if (response.err_num() != 0)
    {
        cerr << response.errmsg() << " register error!" << endl;
    }
    else
    {
        cout << "name register success, userid is " << response.user().id()
                << ", do not forget it!" << endl;
    }
}

void doLoginResponse(const string& responseData)
{
    chat::LoginResponse response;
    if (!response.ParseFromString(responseData)) {
        cerr << "Failed to parse LoginResponse" << endl;
        g_isLoginSuccess = false;
        return;
    }
    
    if (response.err_num() != 0)
    {
        cerr << response.errmsg() << endl;
        g_isLoginSuccess = false;
    }
    else
    {
        g_currentUser.setId(response.user().id());
        g_currentUser.setName(response.user().name());

        g_currentUserFriendList.clear();
        for (int i = 0; i < response.friends_size(); ++i)
        {
            const chat::User& friendUser = response.friends(i);
            User user;
            user.setId(friendUser.id());
            user.setName(friendUser.name());
            user.setState(friendUser.state());
            g_currentUserFriendList.push_back(user);
        }

        g_currentUserGroupList.clear();
        for (int i = 0; i < response.groups_size(); ++i)
        {
            const chat::GroupInfo& groupInfo = response.groups(i);
            Group group;
            group.setId(groupInfo.id());
            group.setName(groupInfo.groupname());
            group.setDesc(groupInfo.groupdesc());
            
            for (int j = 0; j < groupInfo.users_size(); ++j)
            {
                const chat::GroupUser& groupUser = groupInfo.users(j);
                GroupUser user;
                user.setId(groupUser.id());
                user.setName(groupUser.name());
                user.setState(groupUser.state());
                user.setRole(groupUser.role());
                group.getUsers().push_back(user);
            }
            
            g_currentUserGroupList.push_back(group);
        }

        showCurrentUserData();

        for (int i = 0; i < response.offlinemsg_size(); ++i)
        {
            const string& msgStr = response.offlinemsg(i);
            
            chat::OneChatMessage oneChatMsg;
            if (oneChatMsg.ParseFromString(msgStr)) {
                cout << oneChatMsg.base().time() << " [" << oneChatMsg.base().fromid() << "]" 
                     << " said: " << oneChatMsg.message() << endl;
                continue;
            }
            
            chat::GroupChatMessage groupChatMsg;
            if (groupChatMsg.ParseFromString(msgStr)) {
                cout << "群消息[" << groupChatMsg.groupid() << "]:" << groupChatMsg.base().time() 
                     << " [" << groupChatMsg.base().fromid() << "]" 
                     << " said: " << groupChatMsg.message() << endl;
                continue;
            }
        }

        g_isLoginSuccess = true;
    }
}

void handleServerMessage(int msgtype, const string& data)
{
    if (chat::ONE_CHAT_MSG == msgtype)
    {
        chat::OneChatMessage chatMsg;
        if (chatMsg.ParseFromString(data)) {
            cout << chatMsg.base().time() << " [" << chatMsg.base().fromid() << "]" 
                 << " said: " << chatMsg.message() << endl;
        }
        return;
    }

    if (chat::GROUP_CHAT_MSG == msgtype)
    {
        chat::GroupChatMessage groupChatMsg;
        if (groupChatMsg.ParseFromString(data)) {
            cout << "群消息[" << groupChatMsg.groupid() << "]:" << groupChatMsg.base().time() 
                 << " [" << groupChatMsg.base().fromid() << "]" 
                 << " said: " << groupChatMsg.message() << endl;
        }
        return;
    }

    if (chat::LOGIN_MSG_ACK == msgtype)
    {
        doLoginResponse(data);
        sem_post(&rwsem);
        return;
    }

    if (chat::REG_MSG_ACK == msgtype)
    {
        doRegResponse(data);
        sem_post(&rwsem);
        return;
    }

    if (chat::ADD_FRIEND_MSG_ACK == msgtype)
    {
        chat::AddFriendResponse response;
        if (response.ParseFromString(data))
        {
            if (response.err_num() == 0)
                cout << "Friend added successfully!" << endl;
            else
                cerr << "Add friend failed: " << response.errmsg() << endl;
        }
        sem_post(&rwsem);
        return;
    }

    if (chat::CREATE_GROUP_MSG_ACK == msgtype)
    {
        chat::CreateGroupResponse response;
        if (response.ParseFromString(data))
        {
            if (response.err_num() == 0)
                cout << "Group created successfully, groupid: " << response.groupid() << endl;
            else
                cerr << "Create group failed: " << response.errmsg() << endl;
        }
        sem_post(&rwsem);
        return;
    }

    if (chat::ADD_GROUP_MSG_ACK == msgtype)
    {
        chat::AddGroupResponse response;
        if (response.ParseFromString(data))
        {
            if (response.err_num() == 0)
                cout << "Joined group successfully!" << endl;
            else
                cerr << "Join group failed: " << response.errmsg() << endl;
        }
        sem_post(&rwsem);
        return;
    }
}

void readTaskHandler(int clientfd)
{
    string inputBuffer;
    char buf[65536];

    for (;;)
    {
        int len = recv(clientfd, buf, sizeof(buf), 0);
        if (len <= 0)
        {
            cerr << "disconnected from server" << endl;
            close(clientfd);
            exit(-1);
        }

        inputBuffer.append(buf, len);

        while (inputBuffer.size() >= 8)
        {
            int32_t bodyLen = (unsigned char)inputBuffer[0] << 24 |
                              (unsigned char)inputBuffer[1] << 16 |
                              (unsigned char)inputBuffer[2] << 8 |
                              (unsigned char)inputBuffer[3];

            if (bodyLen <= 4 || bodyLen > 65536)
            {
                inputBuffer.erase(0, 4);
                continue;
            }

            int totalLen = 4 + bodyLen;
            if (inputBuffer.size() < (size_t)totalLen) break;

            int32_t msgid = (unsigned char)inputBuffer[4] << 24 |
                            (unsigned char)inputBuffer[5] << 16 |
                            (unsigned char)inputBuffer[6] << 8 |
                            (unsigned char)inputBuffer[7];

            string payload = inputBuffer.substr(8, bodyLen - 4);
            inputBuffer.erase(0, totalLen);

            handleServerMessage(msgid, payload);
        }
    }
}

void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

void help(int fd = 0, string str = "");
void chat_cmd(int, string);
void addfriend(int, string);
void creategroup(int, string);
void addgroup(int, string);
void groupchat(int, string);
void loginout(int, string);

unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式help"},
    {"chat", "一对一聊天，格式chat:friendid:message"},
    {"addfriend", "添加好友，格式addfriend:friendid"},
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式addgroup:groupid"},
    {"groupchat", "群聊，格式groupchat:groupid:message"},
    {"loginout", "注销，格式loginout"}};

unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat_cmd},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};

void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        if (cin.fail()) break;
        string commandbuf(buffer);
        string command;
        int idx = commandbuf.find(":");
        if (-1 == idx)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invalid input command!" << endl;
            continue;
        }

        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx));
    }
}

void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str());
    string buffer = ClientProto::createAddFriendRequest(g_currentUser.getId(), friendid);

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send addfriend msg error" << endl;
    }
    else
    {
        waitForResponse();
    }
}

void chat_cmd(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    int64_t now = time(nullptr);
    string buffer = ClientProto::createOneChatMessage(g_currentUser.getId(), friendid, message, now);

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send chat msg error" << endl;
    }
}

void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() - idx);

    string buffer = ClientProto::createCreateGroupRequest(g_currentUser.getId(), groupname, groupdesc);

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send creategroup msg error" << endl;
    }
    else
    {
        waitForResponse();
    }
}

void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    string buffer = ClientProto::createAddGroupRequest(g_currentUser.getId(), groupid);

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send addgroup msg error" << endl;
    }
    else
    {
        waitForResponse();
    }
}

void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    int64_t now = time(nullptr);
    string buffer = ClientProto::createGroupChatMessage(g_currentUser.getId(), groupid, message, now);

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send groupchat msg error" << endl;
    }
}

void loginout(int clientfd, string)
{
    string buffer = ClientProto::createLogoutRequest(g_currentUser.getId());

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false;
    }   
}
