#!/bin/bash
# 离线镜像导入和启动脚本

echo "离线镜像导入和启动脚本"

# 1. 检查是否已经存在所需的镜像
echo "1. 检查是否已经存在所需的镜像..."
MYSQL_EXISTS=$(docker images -q mysql:8.0)
REDIS_EXISTS=$(docker images -q redis:7-alpine)

if [ -n "$MYSQL_EXISTS" ] && [ -n "$REDIS_EXISTS" ]; then
    echo "所需镜像已存在，直接启动容器..."
    docker compose -f docker-compose-official.yml up -d mysql redis
    exit 0
fi

# 2. 如果镜像不存在，尝试从本地tar文件导入
echo "2. 尝试从本地tar文件导入镜像..."
if [ -f mysql-8.0.tar ]; then
    echo "导入MySQL镜像..."
    docker load -i mysql-8.0.tar
else
    echo "未找到MySQL镜像文件 mysql-8.0.tar"
fi

if [ -f redis-7-alpine.tar ]; then
    echo "导入Redis镜像..."
    docker load -i redis-7-alpine.tar
else
    echo "未找到Redis镜像文件 redis-7-alpine.tar"
fi

# 3. 再次检查镜像是否存在
MYSQL_EXISTS=$(docker images -q mysql:8.0)
REDIS_EXISTS=$(docker images -q redis:7-alpine)

if [ -n "$MYSQL_EXISTS" ] && [ -n "$REDIS_EXISTS" ]; then
    echo "镜像导入成功，启动容器..."
    docker compose -f docker-compose-official.yml up -d mysql redis
    exit 0
fi

# 4. 如果仍然没有镜像，尝试使用配置的镜像加速器拉取
echo "3. 尝试使用配置的镜像加速器拉取镜像..."
echo "拉取MySQL镜像..."
docker pull mysql:8.0

if [ $? -eq 0 ]; then
    echo "MySQL镜像拉取成功"
else
    echo "MySQL镜像拉取失败"
fi

echo "拉取Redis镜像..."
docker pull redis:7-alpine

if [ $? -eq 0 ]; then
    echo "Redis镜像拉取成功"
else
    echo "Redis镜像拉取失败"
fi

# 5. 启动容器
echo "4. 启动容器..."
docker compose -f docker-compose-official.yml up -d mysql redis

echo "脚本执行完成"