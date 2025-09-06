#!/bin/bash
# 改进的Docker镜像加速器测试脚本

echo "改进的Docker镜像加速器测试..."

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

# 测试拉取Redis镜像（相对较小）
echo "3. 测试拉取Redis镜像..."
sudo docker pull redis:7-alpine

if [ $? -eq 0 ]; then
    echo "成功拉取Redis镜像"
    sudo docker rmi redis:7-alpine
else
    echo "拉取Redis镜像失败"
fi

# 测试拉取MySQL镜像（较大，可能需要多次尝试）
echo "4. 测试拉取MySQL镜像（最多尝试3次）..."
for i in {1..3}; do
    echo "尝试第 $i 次拉取MySQL镜像..."
    sudo docker pull mysql:8.0
    
    if [ $? -eq 0 ]; then
        echo "成功拉取MySQL镜像"
        sudo docker rmi mysql:8.0
        break
    else
        echo "第 $i 次拉取MySQL镜像失败"
        if [ $i -lt 3 ]; then
            echo "等待10秒后重试..."
            sleep 10
        fi
    fi
done

echo "测试完成"