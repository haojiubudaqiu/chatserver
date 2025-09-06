# 高性能聊天服务器部署指南

本文档详细说明如何部署高性能聊天服务器集群，包括单机部署和Docker容器化部署两种方式。

## 1. 系统要求

### 1.1 硬件要求
- CPU: 4核或以上
- 内存: 8GB或以上
- 硬盘: 50GB可用空间
- 网络: 千兆网卡

### 1.2 软件要求
- 操作系统: Ubuntu 20.04 LTS 或 CentOS 8+
- 编译工具: GCC 7.0+、CMake 3.0+
- 依赖库:
  - muduo网络库
  - MySQL 8.0+
  - Redis 6.0+
  - Protobuf 3.0+
  - Kafka (可选)

## 2. 依赖库安装

### 2.1 Ubuntu/Debian系统

```bash
# 更新包管理器
sudo apt update

# 安装基础编译工具
sudo apt install -y build-essential cmake git

# 安装数据库客户端
sudo apt install -y libmysqlclient-dev mysql-client

# 安装Redis客户端
sudo apt install -y libhiredis-dev redis-server

# 安装Protobuf
sudo apt install -y libprotobuf-dev protobuf-compiler

# 安装Boost库
sudo apt install -y libboost-all-dev

# 安装muduo网络库
git clone https://github.com/chenshuo/muduo.git
cd muduo
make -j$(nproc)
sudo make install
```

### 2.2 CentOS/RHEL系统

```bash
# 安装EPEL仓库
sudo yum install -y epel-release

# 安装基础编译工具
sudo yum install -y gcc gcc-c++ make cmake git

# 安装数据库客户端
sudo yum install -y mysql-devel

# 安装Redis客户端
sudo yum install -y hiredis-devel redis

# 安装Protobuf
sudo yum install -y protobuf-devel protobuf-compiler

# 安装Boost库
sudo yum install -y boost-devel
```

## 3. 源代码获取与编译

### 3.1 获取源代码
```bash
git clone <repository-url>
cd chatserver
```

### 3.2 编译项目
```bash
# 创建构建目录
mkdir -p build
cd build

# 配置构建
cmake ..

# 编译项目
make -j$(nproc)

# 编译完成后的可执行文件位于bin目录
ls ../bin/
```

## 4. 数据库配置

### 4.1 MySQL配置
```bash
# 启动MySQL服务
sudo systemctl start mysql

# 登录MySQL
mysql -u root -p

# 创建数据库和用户
CREATE DATABASE chat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER 'chatuser'@'localhost' IDENTIFIED BY 'chatpass123';
GRANT ALL PRIVILEGES ON chat.* TO 'chatuser'@'localhost';
FLUSH PRIVILEGES;
```

### 4.2 导入数据库结构
```bash
# 使用项目中的SQL文件初始化数据库
mysql -u chatuser -p chat < docker/mysql/init.sql
```

### 4.3 Redis配置
```bash
# 启动Redis服务
sudo systemctl start redis

# 测试Redis连接
redis-cli ping
```

## 5. 单机部署

### 5.1 启动单个服务器实例
```bash
# 启动聊天服务器
./bin/ChatServer 127.0.0.1 6000
```

### 5.2 启动多个服务器实例（集群模式）
```bash
# 启动第一个实例
./bin/ChatServer 127.0.0.1 6000 &

# 启动第二个实例
./bin/ChatServer 127.0.0.1 6001 &

# 启动第三个实例
./bin/ChatServer 127.0.0.1 6002 &
```

## 6. Docker容器化部署

### 6.1 安装Docker和Docker Compose
```bash
# 安装Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# 安装Docker Compose
sudo curl -L "https://github.com/docker/compose/releases/download/v2.20.0/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
sudo chmod +x /usr/local/bin/docker-compose
```

### 6.2 构建和启动服务
```bash
# 构建所有服务
docker-compose build

# 启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps

# 查看日志
docker-compose logs -f
```

### 6.3 停止服务
```bash
# 停止所有服务
docker-compose down

# 停止并删除数据卷
docker-compose down -v
```

## 7. Nginx负载均衡配置

### 7.1 安装Nginx
```bash
sudo apt install -y nginx
```

### 7.2 配置Nginx
```bash
# 复制配置文件
sudo cp nginx.conf /etc/nginx/nginx.conf

# 测试配置
sudo nginx -t

# 重新加载配置
sudo nginx -s reload
```

### 7.3 启动Nginx
```bash
sudo systemctl start nginx
sudo systemctl enable nginx
```

## 8. 性能监控

### 8.1 系统监控
```bash
# 监控CPU和内存使用
top

# 监控网络连接
netstat -an | grep :7000

# 监控磁盘IO
iostat -x 1
```

### 8.2 应用监控
```bash
# 查看Nginx状态
curl http://localhost:8080/status

# 查看健康检查
curl http://localhost:8080/health
```

## 9. 故障排除

### 9.1 常见问题

#### 编译错误
```bash
# 清理构建目录
rm -rf build/*
cd build
cmake ..
make
```

#### 数据库连接失败
```bash
# 检查MySQL服务状态
sudo systemctl status mysql

# 检查数据库配置
cat src/server/db/db.cpp
```

#### Redis连接失败
```bash
# 检查Redis服务状态
sudo systemctl status redis

# 测试Redis连接
redis-cli ping
```

### 9.2 日志查看
```bash
# 查看系统日志
sudo journalctl -u nginx
sudo journalctl -u mysql
sudo journalctl -u redis

# 查看应用日志
tail -f /var/log/nginx/chat_*.log
```

## 10. 性能调优

### 10.1 系统级调优
```bash
# 调整文件描述符限制
echo "* soft nofile 65535" >> /etc/security/limits.conf
echo "* hard nofile 65535" >> /etc/security/limits.conf

# 调整内核参数
echo "net.core.somaxconn = 65535" >> /etc/sysctl.conf
echo "net.ipv4.tcp_max_syn_backlog = 65535" >> /etc/sysctl.conf
sysctl -p
```

### 10.2 应用级调优
- 调整连接池大小
- 优化数据库索引
- 合理设置缓存过期时间
- 监控和调整线程池大小

## 11. 安全配置

### 11.1 防火墙配置
```bash
# Ubuntu/Debian
sudo ufw allow 7000/tcp
sudo ufw allow 8080/tcp
sudo ufw allow 3306/tcp
sudo ufw allow 6379/tcp

# CentOS/RHEL
sudo firewall-cmd --permanent --add-port=7000/tcp
sudo firewall-cmd --permanent --add-port=8080/tcp
sudo firewall-cmd --permanent --add-port=3306/tcp
sudo firewall-cmd --permanent --add-port=6379/tcp
sudo firewall-cmd --reload
```

### 11.2 SSL/TLS配置
```bash
# 获取SSL证书
sudo apt install -y certbot
sudo certbot certonly --standalone -d yourdomain.com

# 配置Nginx SSL
# 在nginx.conf中添加SSL配置
```

## 12. 备份与恢复

### 12.1 数据库备份
```bash
# 备份数据库
mysqldump -u chatuser -p chat > chat_backup_$(date +%Y%m%d).sql

# 恢复数据库
mysql -u chatuser -p chat < chat_backup_$(date +%Y%m%d).sql
```

### 12.2 配置文件备份
```bash
# 备份配置文件
tar -czf chat_config_backup_$(date +%Y%m%d).tar.gz \
    /etc/nginx/nginx.conf \
    docker/mysql/init.sql \
    src/server/db/db.cpp
```

## 13. 升级维护

### 13.1 代码升级
```bash
# 拉取最新代码
git pull origin main

# 重新编译
cd build
make clean
cmake ..
make -j$(nproc)

# 重启服务
docker-compose down
docker-compose up -d
```

### 13.2 滚动升级
```bash
# 逐个重启服务器实例以实现零停机升级
docker-compose restart chat_server_1
sleep 30
docker-compose restart chat_server_2
sleep 30
docker-compose restart chat_server_3
```

## 14. 监控告警

### 14.1 设置监控告警
- 使用Prometheus + Grafana监控系统指标
- 配置邮件或短信告警
- 设置关键指标阈值

### 14.2 关键监控指标
- QPS (每秒查询数)
- 响应时间
- 内存使用率
- CPU使用率
- 数据库连接数
- Redis命中率

通过遵循本部署指南，您可以成功部署一个高性能、高可用的聊天服务器集群，满足大规模并发用户的需求。