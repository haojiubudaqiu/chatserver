# 定义源文件列表 - 使用更清晰的方式
# 查找并编译proto文件
file(GLOB ProtoFiles "${CMAKE_CURRENT_SOURCE_DIR}/proto/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${ProtoFiles})

file(GLOB SRC_FILES "*.cpp")
file(GLOB DB_FILES "db/*.cpp")
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
)

# 指定可执行文件链接时需要依赖的库文件
# 关键：正确的链接顺序
target_link_libraries(ChatServer 
    muduo_net      # 网络核心库
    muduo_base     # 基础库
    mysqlclient    # MySQL客户端库
    hiredis        # Redis客户端库
    pthread        # 线程库
    ${PROTOBUF_LIBRARIES}  # Protobuf库
    # rdkafka     # Kafka客户端库（如果安装了的话）
)

# 可选：设置编译器特性
set_target_properties(ChatServer PROPERTIES
    CXX_STANDARD 11
    CXX_STANDARD_REQUIRED ON
)