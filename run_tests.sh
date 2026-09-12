#!/bin/bash
# ChatPulse 测试套件入口
# 用法:
#   ./run_tests.sh              # 运行可离线执行的单元测试
#   ./run_tests.sh --all        # 单元测试 + 需要运行中服务器的集成测试
#
# 单元测试（无需任何服务）:
#   - C++:        test_db_pool / test_models / test_redis / test_kafka
#   - Python:     agent_service pytest（14 项）、frontend/bridge pytest（10 项）
#
# 集成测试（需要 docker compose up -d + 服务器运行）:
#   - test_full.sh / test_agent_e2e.sh / test_cross_server.sh / test_persistence.sh

set -e
cd "$(dirname "$0")"

echo "==========================================="
echo "      ChatPulse 单元测试套件"
echo "==========================================="

# 1. C++ 单元测试（编译产物在 bin/，需先构建；C++ 二进制只能在本机平台执行）
echo
echo "[1/3] C++ 单元测试"
if [ "$(uname -s)" != "Linux" ]; then
    echo "  (跳过: C++ 测试需要 Linux 环境，请在 Ubuntu 虚拟机/Docker 中运行)"
elif [ -d bin ]; then
    for t in test_db_pool test_models test_redis test_kafka; do
        if [ -f "bin/$t" ]; then
            echo "  → $t"
            "./bin/$t"
        else
            echo "  (跳过 $t: bin/$t 不存在，请先 cmake + make)"
        fi
    done
else
    echo "  (跳过: bin/ 目录不存在，请先 cmake + make)"
fi

# 2. agent_service 单元测试
echo
echo "[2/3] AI Agent 单元测试 (agent_service)"
if [ -d agent_service/tests ]; then
    (cd agent_service && python -m pytest tests -q) || exit 1
fi

# 3. bridge 单元测试
echo
echo "[3/3] Bridge 单元测试 (frontend/bridge)"
if [ -d frontend/bridge/tests ]; then
    (cd frontend/bridge && python -m pytest tests -q) || exit 1
fi

echo
echo "==========================================="
echo "   单元测试全部通过 ✅"
echo "==========================================="

if [ "$1" = "--all" ]; then
    echo
    echo "开始集成测试（需要服务器正在运行）..."
    ./test_full.sh
    ./test_agent_e2e.sh
    ./test_cross_server.sh
    ./test_persistence.sh
fi
