# Nginx负载均衡配置指南

本文档详细说明如何配置Nginx以实现聊天服务器的负载均衡，支持高并发和高可用性部署。

## 1. Nginx安装

### Ubuntu/Debian系统
```bash
sudo apt update
sudo apt install nginx
```

### CentOS/RHEL系统
```bash
sudo yum install nginx
# 或者对于较新版本
sudo dnf install nginx
```

## 2. Nginx配置文件结构

Nginx配置文件通常位于以下位置：
- 主配置文件：`/etc/nginx/nginx.conf`
- 站点配置：`/etc/nginx/sites-available/`
- 启用站点：`/etc/nginx/sites-enabled/`

## 3. TCP负载均衡配置

### 3.1 Stream模块配置

在`/etc/nginx/nginx.conf`中添加以下配置：

```nginx
# 在http块之前添加stream块
stream {
    # 定义上游服务器组
    upstream chat_backend {
        # 服务器列表 - 根据实际部署情况修改IP和端口
        server 127.0.0.1:6000 weight=3 max_fails=3 fail_timeout=30s;
        server 127.0.0.1:6001 weight=3 max_fails=3 fail_timeout=30s;
        server 127.0.0.1:6002 weight=2 max_fails=3 fail_timeout=30s;
        server 192.168.1.100:6000 weight=1 max_fails=3 fail_timeout=30s backup;
        
        # 负载均衡算法
        # least_conn;  # 最少连接数
        # ip_hash;     # 基于客户端IP的会话保持
        # hash $remote_addr consistent;  # 一致性哈希
    }
    
    # 服务器监听配置
    server {
        # 监听端口
        listen 7000;
        
        # 代理到上游服务器
        proxy_pass chat_backend;
        
        # 代理超时设置
        proxy_timeout 3s;
        proxy_responses 1;
        
        # 日志配置
        error_log /var/log/nginx/chat_error.log;
        access_log /var/log/nginx/chat_access.log;
    }
}
```

### 3.2 HTTP管理接口配置

在HTTP块中添加管理接口：

```nginx
http {
    # ... 其他HTTP配置 ...
    
    server {
        listen 8080;
        server_name localhost;
        
        # 健康检查接口
        location /health {
            return 200 "OK\n";
            add_header Content-Type text/plain;
        }
        
        # 状态监控接口
        location /status {
            stub_status on;
            access_log off;
            allow 127.0.0.1;
            allow 192.168.0.0/16;
            deny all;
        }
        
        # 负载均衡状态
        location /upstream_conf {
            upstream_conf;
            allow 127.0.0.1;
            deny all;
        }
    }
}
```

## 4. 负载均衡算法说明

### 4.1 轮询（默认）
```nginx
upstream chat_backend {
    server 127.0.0.1:6000;
    server 127.0.0.1:6001;
    server 127.0.0.1:6002;
}
```

### 4.2 加权轮询
```nginx
upstream chat_backend {
    server 127.0.0.1:6000 weight=3;
    server 127.0.0.1:6001 weight=2;
    server 127.0.0.1:6002 weight=1;
}
```

### 4.3 最少连接数
```nginx
upstream chat_backend {
    least_conn;
    server 127.0.0.1:6000;
    server 127.0.0.1:6001;
    server 127.0.0.1:6002;
}
```

### 4.4 IP哈希（会话保持）
```nginx
upstream chat_backend {
    ip_hash;
    server 127.0.0.1:6000;
    server 127.0.0.1:6001;
    server 127.0.0.1:6002;
}
```

## 5. 高可用性配置

### 5.1 健康检查
```nginx
upstream chat_backend {
    server 127.0.0.1:6000 max_fails=3 fail_timeout=30s;
    server 127.0.0.1:6001 max_fails=3 fail_timeout=30s;
    server 127.0.0.1:6002 max_fails=3 fail_timeout=30s;
}
```

### 5.2 备用服务器
```nginx
upstream chat_backend {
    server 127.0.0.1:6000;
    server 127.0.0.1:6001;
    server 127.0.0.1:6002 backup;
}
```

## 6. SSL/TLS配置

### 6.1 TCP SSL代理
```nginx
stream {
    upstream chat_backend {
        server 127.0.0.1:6000;
        server 127.0.0.1:6001;
    }
    
    server {
        listen 7000 ssl;
        proxy_pass chat_backend;
        
        ssl_certificate /path/to/certificate.crt;
        ssl_certificate_key /path/to/private.key;
        ssl_protocols TLSv1.2 TLSv1.3;
        ssl_ciphers HIGH:!aNULL:!MD5;
    }
}
```

## 7. 性能优化配置

### 7.1 工作进程优化
```nginx
# nginx.conf
worker_processes auto;
worker_connections 1024;
worker_rlimit_nofile 65535;

events {
    use epoll;
    worker_connections 1024;
    multi_accept on;
}
```

### 7.2 缓冲区优化
```nginx
stream {
    server {
        listen 7000;
        
        # 缓冲区设置
        proxy_buffer_size 16k;
        proxy_buffers 8 16k;
        
        proxy_pass chat_backend;
    }
}
```

## 8. 日志和监控

### 8.1 访问日志格式
```nginx
log_format chat_log '$remote_addr - $remote_user [$time_local] '
                    '"$request" $status $body_bytes_sent '
                    '"$http_referer" "$http_user_agent" '
                    'rt=$request_time uct="$upstream_connect_time" '
                    'uht="$upstream_header_time" urt="$upstream_response_time"';

stream {
    server {
        listen 7000;
        access_log /var/log/nginx/chat_access.log chat_log;
        error_log /var/log/nginx/chat_error.log;
        
        proxy_pass chat_backend;
    }
}
```

## 9. 部署步骤

### 9.1 启动多个聊天服务器实例
```bash
# 启动第一个实例
./bin/ChatServer 127.0.0.1 6000 &

# 启动第二个实例
./bin/ChatServer 127.0.0.1 6001 &

# 启动第三个实例
./bin/ChatServer 127.0.0.1 6002 &
```

### 9.2 配置并启动Nginx
```bash
# 测试配置文件
sudo nginx -t

# 重新加载配置
sudo nginx -s reload

# 或重启Nginx
sudo systemctl restart nginx
```

## 10. 故障排除

### 10.1 检查Nginx状态
```bash
sudo systemctl status nginx
```

### 10.2 查看日志
```bash
# 错误日志
tail -f /var/log/nginx/error.log

# 访问日志
tail -f /var/log/nginx/chat_access.log
```

### 10.3 测试连接
```bash
# 测试TCP连接
telnet localhost 7000

# 测试健康检查接口
curl http://localhost:8080/health
```

## 11. 性能调优建议

1. **连接数优化**：根据服务器性能调整`worker_connections`
2. **内存优化**：合理设置缓冲区大小
3. **SSL优化**：启用会话复用和HTTP/2
4. **压缩**：对于HTTP接口启用gzip压缩
5. **缓存**：适当使用代理缓存

## 12. 安全考虑

1. **访问控制**：限制管理接口的访问IP
2. **SSL/TLS**：启用加密传输
3. **日志审计**：记录所有访问日志
4. **防火墙**：配置适当的防火墙规则