# MySQL 主从复制集群 - 半同步复制配置指南

## 架构概述

```
                    ┌─────────────────┐
                    │   主库 (Master) │
                    │   Port: 3306   │
                    └────────┬────────┘
                             │ 同步复制
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
     ┌────────────┐  ┌────────────┐  ┌────────────┐
     │ 从库1      │  │ 从库2      │  │  应用服务器│
     │ Port:3307  │  │ Port:3308  │  │ (连接池)   │
     └────────────┘  └────────────┘  └────────────┘
```

## 启动集群

```bash
# 1. 启动 MySQL 主从集群
docker-compose -f docker-compose-mysql-master-slave.yml up -d

# 2. 进入主库容器配置复制
docker exec -it chat_mysql_master mysql -uroot -p123456

# 3. 创建复制用户（主库执行）
CREATE USER 'repl'@'%' IDENTIFIED BY 'replpass';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'%';
FLUSH PRIVILEGES;

# 4. 获取主库状态
SHOW MASTER STATUS;
# 记录 File 和 Position 值
```

## 配置从库复制

```bash
# 5. 进入从库1容器
docker exec -it chat_mysql_slave1 mysql -uroot -p123456

# 6. 配置主库复制（替换为实际的主库信息）
CHANGE MASTER TO
    MASTER_HOST='mysql-master',
    MASTER_USER='repl',
    MASTER_PASSWORD='replpass',
    MASTER_PORT=3306,
    MASTER_AUTO_POSITION=1;

# 7. 启动复制
START SLAVE;

# 8. 检查复制状态
SHOW SLAVE STATUS\G

# 确保以下两项都为 Yes:
# - Slave_IO_Running: Yes
# - Slave_SQL_Running: Yes
```

## 同样的步骤配置从库2

```bash
docker exec -it chat_mysql_slave2 mysql -uroot -p123456

CHANGE MASTER TO
    MASTER_HOST='mysql-master',
    MASTER_USER='repl',
    MASTER_PASSWORD='replpass',
    MASTER_PORT=3306,
    MASTER_AUTO_POSITION=1;

START SLAVE;
SHOW SLAVE STATUS\G
```

## 验证半同步复制

```sql
-- 主库检查半同步状态
SHOW STATUS LIKE 'Rpl_semi_sync_master_status';
-- 应该显示: ON

-- 从库检查半同步状态  
SHOW STATUS LIKE 'Rpl_semi_sync_slave_status';
-- 应该显示: ON
```

## 应用服务器配置

在 `db.cpp` 中配置连接信息：

```cpp
// 主库: 127.0.0.1:3306
// 从库1: 127.0.0.1:3307
// 从库2: 127.0.0.1:3308

// 使用 Docker 网络时：
// 主库: mysql-master:3306
// 从库1: mysql-slave1:3306
// 从库2: mysql-slave2:3306
```

## 故障转移说明

### 主库故障

1. **检测**: 连接池健康检查发现主库不可用
2. **告警**: 发送告警通知运维
3. **手动提升从库为主库**:
   ```sql
   -- 在要提升的从库上执行
   STOP SLAVE;
   RESET MASTER;
   ```
4. **修改应用配置**: 更新主库地址
5. **其他从库重新指向新主库**

### 从库故障

1. **自动转移**: 连接池自动跳过故障从库
2. **日志记录**: 记录故障从库信息
3. **从库恢复**: 重新加入连接池
