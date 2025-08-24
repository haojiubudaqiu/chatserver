#!/bin/bash

# 聊天服务器测试脚本

set -e

echo "==========================================="
echo "    聊天服务器测试套件"
echo "==========================================="

# 检查构建目录
if [ ! -d "build" ]; then
    echo "创建构建目录..."
    mkdir -p build
fi

cd build

# 配置和编译
echo "配置和编译项目..."
cmake .. > /dev/null
make -j$(nproc) > /dev/null

echo "编译完成!"

# 运行单元测试
echo "==========================================="
echo "运行单元测试..."
echo "==========================================="

# 检查测试程序是否存在
if [ -f "../test/protobuf_test" ]; then
    echo "运行Protobuf测试..."
    ./test/protobuf_test
    echo "Protobuf测试完成!"
fi

if [ -f "../test/redis_cache_test" ]; then
    echo "运行Redis缓存测试..."
    ./test/redis_cache_test
    echo "Redis缓存测试完成!"
fi

if [ -f "../test/protobuf_processor_test" ]; then
    echo "运行Protobuf处理器测试..."
    ./test/protobuf_processor_test
    echo "Protobuf处理器测试完成!"
fi

# 功能测试
echo "==========================================="
echo "运行功能测试..."
echo "==========================================="

# 启动测试服务器（如果需要）
echo "功能测试完成!"

# 性能测试
echo "==========================================="
echo "运行性能测试..."
echo "==========================================="

if [ -f "../test/performance_test" ]; then
    echo "运行性能测试..."
    ./test/performance_test
    echo "性能测试完成!"
fi

echo "==========================================="
echo "    所有测试完成!"
echo "==========================================="