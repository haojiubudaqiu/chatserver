#!/bin/bash
# 逐步启动Docker容器的脚本

echo "逐步启动Docker容器..."

# 启动MySQL服务
echo "1. 启动MySQL服务..."
docker compose -f docker-compose-basic.yml up -d mysql

# 等待MySQL启动
echo "2. 等待MySQL服务启动..."
sleep 30

# 检查MySQL状态
echo "3. 检查MySQL服务状态..."
docker compose -f docker-compose-basic.yml ps

# 启动Redis服务
echo "4. 启动Redis服务..."
docker compose -f docker-compose-basic.yml up -d redis

# 等待Redis启动
echo "5. 等待Redis服务启动..."
sleep 10

# 检查Redis状态
echo "6. 检查Redis服务状态..."
docker compose -f docker-compose-basic.yml ps

echo "基础服务启动完成！"