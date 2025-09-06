# 查找并编译proto文件
file(GLOB ProtoFiles "${PROJECT_SOURCE_DIR}/src/server/proto/*.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${ProtoFiles})

# 定义了一个SRC_LIST变量，包含了该目录下所有的源文件
aux_source_directory(. SRC_LIST)

# 指定生成可执行文件
add_executable(ChatClient ${SRC_LIST} ${PROTO_SRCS})

# 指定客户端需要包含的头文件路径
target_include_directories(ChatClient PRIVATE 
    ${PROJECT_SOURCE_DIR}/include/client
    ${PROJECT_SOURCE_DIR}/src/server/proto
)

# 指定可执行文件链接时需要依赖的库文件
target_link_libraries(ChatClient 
    muduo_net 
    muduo_base 
    pthread
    ${PROTOBUF_LIBRARIES}
)