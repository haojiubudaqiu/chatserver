# 定义源文件列表 - 使用更清晰的方式
# 查找并编译proto文件
file(GLOB ProtoFiles "${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${ProtoFiles})

file(GLOB SRC_FILES "*.cpp")
set(DB_FILES 
    db/db.cpp
    db/connection_pool.cpp
    db/database_router.cpp
)
file(GLOB MODEL_FILES "model/*.cpp")
file(GLOB REDIS_FILES "redis/*.cpp")
file(GLOB LOG_FILES "log/*.cpp")
file(GLOB KAFKA_FILES "kafka/*.cpp")

# 指定生成可执行文件
add_executable(ChatServer 
    ${SRC_FILES}
    ${DB_FILES}
    ${MODEL_FILES}
    ${REDIS_FILES}
    ${LOG_FILES}
    ${KAFKA_FILES}
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

# 指定服务端需要包含的头文件路径
target_include_directories(ChatServer PRIVATE 
    ${PROJECT_SOURCE_DIR}/include/server
    ${CMAKE_CURRENT_SOURCE_DIR}/proto
)

# 检查librdkafka库是否存在
find_library(RDKAFKA_LIBRARY rdkafka)
find_library(RDKAFKA++_LIBRARY rdkafka++)
find_path(RDKAFKA_INCLUDE_DIR librdkafka/rdkafka.h)

if(RDKAFKA_LIBRARY AND RDKAFKA++_LIBRARY AND RDKAFKA_INCLUDE_DIR)
    # 如果找到了librdkafka库，则添加相关配置
    target_link_libraries(ChatServer 
        muduo_net      # 网络核心库
        muduo_base     # 基础库
        mysqlclient    # MySQL客户端库
        hiredis        # Redis客户端库
        ${RDKAFKA_LIBRARY}     # Kafka客户端库
        ${RDKAFKA++_LIBRARY}   # Kafka C++客户端库
        pthread        # 线程库
        ${PROTOBUF_LIBRARIES}  # Protobuf库
    )
    target_include_directories(ChatServer PRIVATE ${RDKAFKA_INCLUDE_DIR})
    target_compile_definitions(ChatServer PRIVATE HAS_LIBRDKAFKA)
else()
    # 如果没有找到librdkafka库，则不链接相关库
    target_link_libraries(ChatServer 
        muduo_net      # 网络核心库
        muduo_base     # 基础库
        mysqlclient    # MySQL客户端库
        hiredis        # Redis客户端库
        pthread        # 线程库
        ${PROTOBUF_LIBRARIES}  # Protobuf库
    )
endif()

# 可选：设置编译器特性
set_target_properties(ChatServer PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)