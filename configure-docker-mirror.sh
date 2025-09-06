#!/bin/bash
# Docker镜像加速器配置脚本

echo "开始配置Docker镜像加速器..."

# 1. 创建Docker配置目录
echo "1. 创建Docker配置目录..."
sudo mkdir -p /etc/docker

# 2. 创建Docker daemon配置文件
echo "2. 创建Docker daemon配置文件..."
sudo tee /etc/docker/daemon.json > /dev/null <<EOF
{
  "registry-mirrors": [
    "https://mirror.ccs.tencentyun.com",
    "https://docker.mirrors.ustc.edu.cn",
    "https://hub-mirror.c.163.com",
    "https://mirror.baidubce.com"
  ]
}
EOF

echo "Docker daemon配置文件已创建:"
sudo cat /etc/docker/daemon.json

# 3. 重启Docker服务
echo "3. 重启Docker服务..."
sudo systemctl restart docker

# 4. 验证Docker服务状态
echo "4. 验证Docker服务状态..."
sudo systemctl status docker

echo "Docker镜像加速器配置完成！"
echo "现在你可以使用以下命令启动容器:"
echo "cd /home/song/workspace/chatserver"
echo "docker compose -f docker-compose-domestic.yml up -d"