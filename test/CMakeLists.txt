cmake_minimum_required(VERSION 3.0)
project(chat_test)

# Common include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/../include/server
    ${PROJECT_SOURCE_DIR}/../include/server/db
    ${PROJECT_SOURCE_DIR}/../include/server/model
    ${PROJECT_SOURCE_DIR}/../include/server/redis
    ${PROJECT_SOURCE_DIR}/../include/server/kafka
    ${PROJECT_SOURCE_DIR}/../include/server/log
    ${PROJECT_SOURCE_DIR}/../src/server/proto
    /usr/include/mysql
    /usr/include/hiredis
    /home/song/.local/include
)

# Find libraries
find_library(RDKAFKA_LIBRARY rdkafka)
find_library(RDKAFKA++_LIBRARY rdkafka++)

# Add mock implementations or compile actual source files
# Instead of compiling the whole server, we just include the needed CPP files to test linkage
set(SERVER_SRC_DIR ${PROJECT_SOURCE_DIR}/../src/server)

# Test DB Pool
add_executable(test_db_pool test_db_pool.cpp 
    ${SERVER_SRC_DIR}/db/db.cpp
    ${SERVER_SRC_DIR}/db/connection_pool.cpp 
    ${SERVER_SRC_DIR}/db/database_router.cpp)
target_link_libraries(test_db_pool pthread mysqlclient muduo_base muduo_net)

# Test Redis
add_executable(test_redis test_redis.cpp 
    ${SERVER_SRC_DIR}/redis/redis_cache.cpp
    ${SERVER_SRC_DIR}/redis/redis_sentinel.cpp)
target_link_libraries(test_redis pthread hiredis muduo_base muduo_net)

# Test Models
add_executable(test_models test_models.cpp 
    ${SERVER_SRC_DIR}/model/usermodel.cpp
    ${SERVER_SRC_DIR}/model/friendmodel.cpp
    ${SERVER_SRC_DIR}/model/groupmodel.cpp
    ${SERVER_SRC_DIR}/model/offlinemessagemodel.cpp
    ${SERVER_SRC_DIR}/db/db.cpp
    ${SERVER_SRC_DIR}/db/connection_pool.cpp 
    ${SERVER_SRC_DIR}/db/database_router.cpp
    ${SERVER_SRC_DIR}/redis/redis_cache.cpp
    ${SERVER_SRC_DIR}/redis/redis_sentinel.cpp
    ${SERVER_SRC_DIR}/redis/cache_manager.cpp)
target_link_libraries(test_models pthread mysqlclient hiredis muduo_base muduo_net)

# Test Kafka
add_executable(test_kafka test_kafka.cpp 
    ${SERVER_SRC_DIR}/kafka/kafka_manager.cpp
    ${SERVER_SRC_DIR}/kafka/kafka_producer.cpp
    ${SERVER_SRC_DIR}/kafka/kafka_consumer.cpp)
if(RDKAFKA_LIBRARY)
    target_link_libraries(test_kafka pthread ${RDKAFKA_LIBRARY} ${RDKAFKA++_LIBRARY} muduo_base muduo_net)
    target_compile_definitions(test_kafka PRIVATE HAS_LIBRDKAFKA)
else()
    target_link_libraries(test_kafka pthread muduo_base muduo_net)
endif()

