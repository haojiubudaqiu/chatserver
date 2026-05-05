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
        cin.get();

        switch (choice)
        {
        case 1:
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;
            cin.get();
            cout << "userpassword:";
            cin.getline(pwd, 50);

            string request = ClientProto::createLoginRequest(id, pwd);
            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), request.size(), 0);
            if (len == -1)
            {
                cerr << "send login msg error:" << request << endl;
            }

            sem_wait(&rwsem);
                
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
                cerr << "send reg msg error:" << request << endl;
            }
            
            sem_wait(&rwsem);
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

void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[4096] = {0};
        int len = recv(clientfd, buffer, sizeof(buffer) - 1, 0);
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }
        
        buffer[len] = '\0';

        chat::BaseMessage baseMsg;
        if (!baseMsg.ParseFromString(string(buffer, len))) {
            cerr << "Failed to parse base message" << endl;
            continue;
        }
        
        int msgtype = baseMsg.msgid();
        
        if (ONE_CHAT_MSG == msgtype)
        {
            chat::OneChatMessage chatMsg;
            if (chatMsg.ParseFromString(string(buffer, len))) {
                cout << chatMsg.base().time() << " [" << chatMsg.base().fromid() << "]" 
                     << " said: " << chatMsg.message() << endl;
            }
            continue;
        }

        if (GROUP_CHAT_MSG == msgtype)
        {
            chat::GroupChatMessage groupChatMsg;
            if (groupChatMsg.ParseFromString(string(buffer, len))) {
                cout << "群消息[" << groupChatMsg.groupid() << "]:" << groupChatMsg.base().time() 
                     << " [" << groupChatMsg.base().fromid() << "]" 
                     << " said: " << groupChatMsg.message() << endl;
            }
            continue;
        }

        if (LOGIN_MSG_ACK == msgtype)
        {
            doLoginResponse(string(buffer, len));
            sem_post(&rwsem);
            continue;
        }

        if (REG_MSG_ACK == msgtype)
        {
            doRegResponse(string(buffer, len));
            sem_post(&rwsem);
            continue;
        }

        if (ADD_FRIEND_MSG_ACK == msgtype)
        {
            chat::AddFriendResponse response;
            if (response.ParseFromString(string(buffer, len)))
            {
                if (response.err_num() == 0)
                    cout << "Friend added successfully!" << endl;
                else
                    cerr << "Add friend failed: " << response.errmsg() << endl;
            }
            sem_post(&rwsem);
            continue;
        }

        if (CREATE_GROUP_MSG_ACK == msgtype)
        {
            chat::CreateGroupResponse response;
            if (response.ParseFromString(string(buffer, len)))
            {
                if (response.err_num() == 0)
                    cout << "Group created successfully, groupid: " << response.groupid() << endl;
                else
                    cerr << "Create group failed: " << response.errmsg() << endl;
            }
            sem_post(&rwsem);
            continue;
        }

        if (ADD_GROUP_MSG_ACK == msgtype)
        {
            chat::AddGroupResponse response;
            if (response.ParseFromString(string(buffer, len)))
            {
                if (response.err_num() == 0)
                    cout << "Joined group successfully!" << endl;
                else
                    cerr << "Join group failed: " << response.errmsg() << endl;
            }
            sem_post(&rwsem);
            continue;
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
void chat(int, string);
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
    {"chat", chat},
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
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
    else
    {
        sem_wait(&rwsem);
    }
}

void chat(int clientfd, string str)
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
        cerr << "send chat msg error -> " << buffer << endl;
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
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
    else
    {
        sem_wait(&rwsem);
    }
}

void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    string buffer = ClientProto::createAddGroupRequest(g_currentUser.getId(), groupid);

    int len = send(clientfd, buffer.c_str(), buffer.size(), 0);
    if (len == -1)
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
    else
    {
        sem_wait(&rwsem);
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
        cerr << "send groupchat msg error -> " << buffer << endl;
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
