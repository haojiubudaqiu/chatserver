#!/bin/bash
set -e
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
echo "清理构建目录: ${BUILD_DIR}"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
echo "执行 CMake 配置..."
cmake .. || { echo "CMake 配置失败！"; echo "请检查 CMakeLists.txt 文件"; exit 1; }
echo "开始编译（使用所有 CPU 核心）..."
make -j$(nproc) || { echo "编译失败！"; echo "请检查源代码错误"; exit 1; }
cd "${SCRIPT_DIR}"
echo "==========================================="
echo "编译成功！可执行文件位置:"
find "${BUILD_DIR}" -type f -executable -print
echo "==========================================="
