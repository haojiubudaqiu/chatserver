-- 创建聊天数据库
CREATE DATABASE IF NOT EXISTS chat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE chat;

-- 创建索引优化查询性能

-- 用户表
CREATE TABLE IF NOT EXISTS user (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(50) NOT NULL,
    state ENUM('online', 'offline') DEFAULT 'offline',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

-- 好友关系表
CREATE TABLE IF NOT EXISTS friend (
    userid INT NOT NULL,
    friendid INT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (userid, friendid),
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE,
    FOREIGN KEY (friendid) REFERENCES user(id) ON DELETE CASCADE
);

-- 群组表
CREATE TABLE IF NOT EXISTS allgroup (
    id INT AUTO_INCREMENT PRIMARY KEY,
    groupname VARCHAR(50) NOT NULL UNIQUE,
    groupdesc VARCHAR(200) DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 群组用户关系表
CREATE TABLE IF NOT EXISTS groupuser (
    groupid INT NOT NULL,
    userid INT NOT NULL,
    grouprole ENUM('creator', 'admin', 'normal') DEFAULT 'normal',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (groupid, userid),
    FOREIGN KEY (groupid) REFERENCES allgroup(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE
);

-- 离线消息表
CREATE TABLE IF NOT EXISTS offlinemessage (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    userid INT NOT NULL,
    message TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE
);

-- 插入测试数据
INSERT INTO user (name, password, state) VALUES 
('admin', 'admin123', 'offline'),
('user1', 'pass123', 'offline'),
('user2', 'pass456', 'offline'),
('user3', 'pass789', 'offline')
ON DUPLICATE KEY UPDATE name=name;

-- 创建索引优化查询性能  索引通过牺牲少量的写入性能（因为写入数据时也要更新目录）和存储空间，来极大地提升查询速度。
CREATE INDEX idx_user_state ON user(state); --加速“查询所有在线用户”这类操作。
CREATE INDEX idx_friend_userid ON friend(userid); --加速“查询某用户的所有好友”和“查询谁把我加为好友”的操作。
CREATE INDEX idx_friend_friendid ON friend(friendid);
CREATE INDEX idx_groupuser_userid ON groupuser(userid); --加速“查询某用户加入的所有群”和“查询某群的所有成员”。
CREATE INDEX idx_groupuser_groupid ON groupuser(groupid);
CREATE INDEX idx_offlinemessage_userid ON offlinemessage(userid); --至关重要。用户登录时拉取离线消息，就是通过 userid 来查询的，这个索引能使其速度飞快。
CREATE INDEX idx_offlinemessage_created_at ON offlinemessage(created_at); --可能用于清理过期离线消息的查询