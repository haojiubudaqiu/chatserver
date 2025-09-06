#!/bin/bash
# 测试Docker镜像加速器配置的脚本

echo "测试Docker镜像加速器配置..."

# 检查Docker镜像加速器配置
echo "1. 检查Docker镜像加速器配置..."
sudo docker info | grep -i registry

# 测试拉取一个小型镜像
echo "2. 测试拉取官方alpine镜像..."
sudo docker pull alpine:latest

if [ $? -eq 0 ]; then
    echo "成功拉取alpine镜像，说明镜像加速器配置正确"
    sudo docker rmi alpine:latest
else
    echo "拉取alpine镜像失败，镜像加速器可能未正确配置"
fi

# 测试拉取MySQL镜像
echo "3. 测试拉取MySQL镜像..."
sudo docker pull mysql:8.0

if [ $? -eq 0 ]; then
    echo "成功拉取MySQL镜像"
    sudo docker rmi mysql:8.0
else
    echo "拉取MySQL镜像失败"
fi

echo "测试完成"