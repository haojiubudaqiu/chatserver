#!/bin/bash
# 启动聊天服务器依赖服务的脚本

echo "正在启动MySQL服务..."
docker compose -f docker-compose-domestic.yml up -d mysql
sleep 10

echo "正在启动Redis服务..."
docker compose -f docker-compose-domestic.yml up -d redis
sleep 10

echo "正在启动Zookeeper服务..."
docker compose -f docker-compose-domestic.yml up -d zookeeper
sleep 10

echo "正在启动Kafka服务..."
docker compose -f docker-compose-domestic.yml up -d kafka
sleep 10

echo "正在检查服务状态..."
docker compose -f docker-compose-domestic.yml ps

echo "所有服务已启动！"