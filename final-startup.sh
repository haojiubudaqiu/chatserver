#!/bin/bash
# 最终的Docker容器启动脚本

echo "最终的Docker容器启动脚本"

# 1. 配置更可靠的镜像加速器
echo "1. 配置更可靠的镜像加速器..."
./configure-better-mirror.sh

# 2. 等待配置生效
echo "2. 等待配置生效..."
sleep 10

# 3. 测试镜像拉取
echo "3. 测试镜像拉取..."
./test-docker-mirror-improved.sh

# 4. 尝试启动基础服务
echo "4. 尝试启动基础服务..."
./offline-import-start.sh

# 5. 检查服务状态
echo "5. 检查服务状态..."
docker compose -f docker-compose-official.yml ps

echo "所有步骤执行完成"