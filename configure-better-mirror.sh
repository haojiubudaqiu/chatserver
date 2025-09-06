#!/bin/bash
# 配置更可靠的Docker镜像加速器

echo "配置更可靠的Docker镜像加速器..."

# 1. 备份当前配置文件
echo "1. 备份当前配置文件..."
if [ -f /etc/docker/daemon.json ]; then
    sudo cp /etc/docker/daemon.json /etc/docker/daemon.json.backup.2
    echo "已备份当前配置文件到 /etc/docker/daemon.json.backup.2"
fi

# 2. 复制新的配置文件
echo "2. 复制新的配置文件..."
sudo cp /home/song/workspace/chatserver/daemon-improved.json /etc/docker/daemon.json

echo "新的Docker daemon配置文件已创建:"
sudo cat /etc/docker/daemon.json

# 3. 重启Docker服务
echo "3. 重启Docker服务..."
sudo systemctl restart docker

# 4. 等待Docker服务启动
echo "4. 等待Docker服务启动..."
sleep 10

# 5. 验证Docker服务状态
echo "5. 验证Docker服务状态..."
sudo systemctl status docker --no-pager

# 6. 验证镜像加速器配置
echo "6. 验证镜像加速器配置..."
sudo docker info | grep -i registry

echo "Docker镜像加速器配置更新完成！"