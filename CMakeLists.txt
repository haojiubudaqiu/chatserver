cmake_minimum_required(VERSION 3.0)
project(chat)

# 启用C++14标准以支持std::make_unique
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 查找并包含Protobuf
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

# 配置编译选项 - 简化并添加必要的选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -pthread -Wall -Wextra")

# 配置最终的可执行文件输出的路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 配置头文件的搜索路径 - 简化写法
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/server
    ${PROJECT_SOURCE_DIR}/include/server/db
    ${PROJECT_SOURCE_DIR}/include/server/model
    ${PROJECT_SOURCE_DIR}/include/server/redis
    ${PROJECT_SOURCE_DIR}/include/server/log
    ${PROJECT_SOURCE_DIR}/include/server/kafka
    ${PROJECT_SOURCE_DIR}/thirdparty
    
    # 添加 MySQL 头文件路径
    /usr/include/mysql
    
    # 添加 Redis 头文件路径
    /usr/include/hiredis
)

# 添加库搜索路径
link_directories(
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
)

# 加载子目录
add_subdirectory(src)
add_subdirectory(test)