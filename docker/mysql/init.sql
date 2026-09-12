-- ChatPulse 数据库初始化脚本
-- 在主库和从库上都会执行（从库重放相同脚本保证基础数据一致，再从主库当前位点开始复制）。
-- 所有语句必须可幂等重放（IF NOT EXISTS / ON DUPLICATE KEY），索引一律内联在 CREATE TABLE 中，
-- 避免从库重放主库 binlog 时出现"Duplicate key name"导致复制中断。

-- 创建聊天数据库
CREATE DATABASE IF NOT EXISTS chat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE chat;

-- 用户表
CREATE TABLE IF NOT EXISTS user (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(50) NOT NULL,
    state ENUM('online', 'offline') DEFAULT 'offline',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_user_state (state) -- 加速"查询所有在线用户"这类操作
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友关系表
CREATE TABLE IF NOT EXISTS friend (
    userid INT NOT NULL,
    friendid INT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (userid, friendid),
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE,
    FOREIGN KEY (friendid) REFERENCES user(id) ON DELETE CASCADE,
    INDEX idx_friend_userid (userid),  -- 加速"查询某用户的所有好友"
    INDEX idx_friend_friendid (friendid) -- 加速"查询谁把我加为好友"
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 群组表
CREATE TABLE IF NOT EXISTS allgroup (
    id INT AUTO_INCREMENT PRIMARY KEY,
    groupname VARCHAR(50) NOT NULL UNIQUE,
    groupdesc VARCHAR(200) DEFAULT '',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 群组用户关系表
CREATE TABLE IF NOT EXISTS groupuser (
    groupid INT NOT NULL,
    userid INT NOT NULL,
    grouprole ENUM('creator', 'admin', 'normal') DEFAULT 'normal',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (groupid, userid),
    FOREIGN KEY (groupid) REFERENCES allgroup(id) ON DELETE CASCADE,
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE,
    INDEX idx_groupuser_userid (userid),  -- 加速"查询某用户加入的所有群"
    INDEX idx_groupuser_groupid (groupid) -- 加速"查询某群的所有成员"
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 离线消息表（message 使用 LONGBLOB 存储二进制 protobuf 数据）
CREATE TABLE IF NOT EXISTS offlinemessage (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    userid INT NOT NULL,
    message LONGBLOB NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (userid) REFERENCES user(id) ON DELETE CASCADE,
    INDEX idx_offlinemessage_userid (userid), -- 至关重要：登录拉取离线消息按 userid 查询
    INDEX idx_offlinemessage_created_at (created_at) -- 用于清理过期离线消息
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 聊天消息持久化表（全量历史记录）
CREATE TABLE IF NOT EXISTS chat_message (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    msg_type TINYINT NOT NULL COMMENT '1=private, 2=group',
    from_id INT NOT NULL,
    to_id INT NOT NULL COMMENT 'for private: recipient id, for group: group id',
    content TEXT NOT NULL,
    msg_time BIGINT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (from_id) REFERENCES user(id) ON DELETE CASCADE,
    INDEX idx_chat_private (msg_type, from_id, to_id, msg_time DESC),
    INDEX idx_chat_group (msg_type, to_id, msg_time DESC)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 插入测试数据
INSERT INTO user (name, password, state) VALUES
('admin', 'admin123', 'offline'),
('user1', 'pass123', 'offline'),
('user2', 'pass456', 'offline'),
('user3', 'pass789', 'offline')
ON DUPLICATE KEY UPDATE name=name;

-- 智能 AI 助手（固定 ID 10000）
SET NAMES utf8mb4;
INSERT INTO user (id, name, password, state) VALUES (10000, 'AI智能助手', 'ai_token_123', 'offline')
ON DUPLICATE KEY UPDATE name=name;

-- 创建专用复制账号（从库通过该账号连接主库复制数据）
-- caching_sha2_password 是 MySQL 8.0 默认认证插件，CHANGE MASTER 时需配 GET_MASTER_PUBLIC_KEY=1
CREATE USER IF NOT EXISTS 'repl'@'%' IDENTIFIED WITH caching_sha2_password BY 'repl_pass_123';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';
FLUSH PRIVILEGES;
