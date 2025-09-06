# 查找并包含Protobuf
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIRS})

# 添加头文件搜索路径
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/server
    ${PROJECT_SOURCE_DIR}/include/server/db
    ${PROJECT_SOURCE_DIR}/include/server/model
    ${PROJECT_SOURCE_DIR}/include/server/redis
    ${PROJECT_SOURCE_DIR}/include/server/log
    ${PROJECT_SOURCE_DIR}/include/server/kafka
    ${PROJECT_SOURCE_DIR}/thirdparty
    ${PROJECT_SOURCE_DIR}/src/server/proto
)

# 查找并编译proto文件
file(GLOB ProtoFiles "${PROJECT_SOURCE_DIR}/src/server/proto/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${ProtoFiles})

# Protobuf测试程序
add_executable(protobuf_test protobuf_test.cpp ${PROTO_SRCS})
target_link_libraries(protobuf_test 
    ${PROTOBUF_LIBRARIES}
)

# Protobuf处理器测试程序
add_executable(protobuf_processor_test protobuf_processor_test.cpp ${PROTO_SRCS})
target_link_libraries(protobuf_processor_test 
    ${PROTOBUF_LIBRARIES}
    muduo_net
    muduo_base
)

# Redis缓存测试程序
add_executable(redis_cache_test redis_cache_test.cpp)
target_link_libraries(redis_cache_test 
    hiredis
)

# 性能测试程序
add_executable(performance_test performance_test.cpp)
target_link_libraries(performance_test 
    ${PROTOBUF_LIBRARIES}
    muduo_net
    muduo_base
    pthread
)

# 添加测试目录
add_subdirectory(testMuduo)
# 注释掉testJson目录，因为缺少CMakeLists.txt文件
# add_subdirectory(testJson)