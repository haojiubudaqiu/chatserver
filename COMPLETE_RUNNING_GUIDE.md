# 完整运行指南

## 环境准备

经过检查，系统中已安装以下组件：
- ✅ Protobuf库和编译器
- ✅ Boost库（完整开发包）
- ✅ muduo网络库（位于/usr/local/lib）
- ✅ MySQL客户端库
- ✅ Redis客户端库（hiredis）

缺失的组件：
- ❌ MySQL服务器
- ❌ Redis服务器
- ❌ Kafka（可选）
- ❌ Docker和docker-compose

## 解决方案

由于当前用户没有root权限安装系统服务，推荐以下两种方案：

### 方案一：使用Docker容器化部署（推荐）

1. 获取root权限或联系系统管理员安装Docker：
```bash
# Ubuntu/Debian系统安装命令
sudo apt update
sudo apt install docker.io docker-compose -y
sudo usermod -aG docker $USER
```

2. 使用项目提供的docker-compose配置启动所有服务：
```bash
cd /home/song/workspace/chatserver
docker-compose up -d
```

这将自动启动：
- MySQL数据库服务（包含初始化数据）
- Redis服务
- Kafka服务（可选）
- 多个聊天服务器实例
- Nginx负载均衡器

3. 编译并运行项目：
```bash
cd build
rm -rf *
cmake ..
make
```

4. 启动客户端和服务端：
```bash
# 服务端
./bin/ChatServer 127.0.0.1 6000

# 客户端
./bin/ChatClient 127.0.0.1 6000
```

### 方案二：手动安装依赖服务

如果您有root权限，可以手动安装所有依赖：

#### 1. 安装MySQL服务器
```bash
sudo apt install mysql-server
sudo systemctl start mysql
sudo systemctl enable mysql
```

创建数据库和表：
```sql
CREATE DATABASE chat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE chat;

-- 用户表
CREATE TABLE user (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(50) NOT NULL,
    state ENUM('online', 'offline') DEFAULT 'offline',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- 好友关系表
CREATE TABLE friend (
    userid INT NOT NULL,
    friendid INT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (userid, friendid),
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE,
    FOREIGN KEY (friendid) REFERENCES user(id) ON DELETE CASCADE
);

-- 群组表
CREATE TABLE allgroup (
    id INT AUTO_INCREMENT PRIMARY KEY,
    groupname VARCHAR(50) NOT NULL UNIQUE,
    groupdesc VARCHAR(200) DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 群组用户关系表
CREATE TABLE groupuser (
    groupid INT NOT NULL,
    userid INT NOT NULL,
    grouprole ENUM('creator', 'admin', 'normal') DEFAULT 'normal',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (groupid, userid),
    FOREIGN KEY (groupid) REFERENCES allgroup(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE
);

-- 离线消息表
CREATE TABLE offlinemessage (
    userid INT NOT NULL,
    message VARCHAR(500) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE
);

-- 测试数据
INSERT INTO user (name, password, state) VALUES 
('admin', 'admin123', 'offline'),
('user1', 'pass123', 'offline'),
('user2', 'pass456', 'offline'),
('user3', 'pass789', 'offline');
```

#### 2. 安装Redis服务器
```bash
sudo apt install redis-server
sudo systemctl start redis
sudo systemctl enable redis
```

#### 3. （可选）安装Kafka
```bash
sudo apt install openjdk-11-jdk
wget https://archive.apache.org/dist/kafka/3.0.0/kafka_2.13-3.0.0.tgz
tar -xzf kafka_2.13-3.0.0.tgz
sudo mv kafka_2.13-3.0.0 /opt/kafka
```

#### 4. 编译项目
```bash
cd /home/song/workspace/chatserver/build
rm -rf *
cmake ..
make
```

#### 5. 运行服务
```bash
# 启动服务端
./bin/ChatServer 127.0.0.1 6000

# 启动客户端
./bin/ChatClient 127.0.0.1 6000
```

## 客户端使用说明

### 登录/注册界面
```
========================
1. login
2. register
3. quit
========================
choice:
```

### 注册新用户
选择选项2，输入用户名和密码。

### 用户登录
选择选项1，输入用户ID和密码。

### 聊天命令
登录成功后可使用以下命令：
- `help` : 显示帮助信息
- `chat:friendid:message` : 一对一聊天
- `addfriend:friendid` : 添加好友
- `creategroup:groupname:groupdesc` : 创建群组
- `addgroup:groupid` : 加入群组
- `groupchat:groupid:message` : 群聊
- `loginout` : 注销

## 数据库配置

如果需要修改数据库连接信息，请编辑：
`/home/song/workspace/chatserver/src/server/db/db.cpp`

默认配置：
- 服务器: 127.0.0.1
- 用户名: root
- 密码: Sf523416&111
- 数据库: chat
- 端口: 3306

## 故障排除

### 编译错误
如果遇到链接错误，请确保：
1. muduo库已正确安装在/usr/local/lib
2. 所有依赖库都已安装
3. CMake能够找到所有必需的库

### 连接错误
如果客户端无法连接服务端：
1. 检查服务端是否正常运行
2. 确认端口没有被防火墙阻止
3. 验证IP地址和端口号正确

### 数据库错误
如果服务端无法连接数据库：
1. 确认MySQL服务正在运行
2. 验证数据库配置信息正确
3. 检查MySQL用户权限

## 性能和扩展

项目支持集群部署：
- 可以启动多个服务实例
- 使用Redis进行消息传递
- Nginx负载均衡支持
- Kafka消息队列（可选）