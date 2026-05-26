# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

This is a cluster chat server developed based on the muduo network library for Linux environments. It implements features such as user registration, login, adding friends, creating/joining groups, private messaging, group chat, and offline message storage.

## Build System

The project uses CMake as its build system:
- Main `CMakeLists.txt` in the root directory
- Separate `CMakeLists.txt` for server, client, and test components
- Executables are output to the `bin/` directory

### Build Commands

```bash
cd build && cmake .. && make
# Or: ./autobuild.sh
```

### Dependencies

- muduo network library (muduo_net, muduo_base)
- MySQL client library (mysqlclient)
- Redis client library (hiredis) — for caching only
- Protobuf library (protobuf) — sole serialization format
- librdkafka (optional) — Kafka message queue for cross-server messaging
- pthread

## Architecture

### Server Components

1. **ChatServer** — Network layer using muduo TcpServer, handles connections and I/O
2. **ChatService** — Business logic layer (singleton), routes messages by Protobuf message ID
3. **Data Models** — ORM classes: `UserModel`, `FriendModel`, `GroupModel`, `OfflineMsgModel`
4. **Database Layer** — `MySQL` wrapper, `ConnectionPool` (master/slave), `DatabaseRouter` (read/write split)
5. **Redis Cache Layer** — `RedisCache` (hiredis wrapper), `CacheManager` (high-level cache ops), `RedisSentinel` (HA with Sentinel)
6. **Kafka Layer** — `KafkaProducer`, `KafkaConsumer`, `KafkaManager` (cross-server message passing)
7. **Async Logging** — `AsyncLogging` (muduo-style double-buffered async logger)
8. **Protobuf** — `message.proto`, `ProtoMsgHandlerMap` (message ID → handler binding)
9. **MCP Server** — `ChatMcpServer` (HTTP-based MCP server for AI agent integration), embeds `c++_mcp/` library

### Cross-Server Communication Architecture

```
Server A (user A) ──→ sends msg to user B
  ├── user B on same server? → direct delivery
  └── user B not local? → publish to Kafka topic "user_messages"
                           ↓
          ALL servers' Kafka consumers receive the message
                           ↓
          Each server checks _userConnMap for user B
          Only the server with user B connected delivers
```

**Important**: Redis is used ONLY for data caching (user info, friend lists, group info, online status). Kafka is the sole mechanism for cross-server message passing. The old Redis Pub/Sub pattern (`redis.hpp` / `redis.cpp`) has been removed.

### Key Design Patterns

- Reactor pattern (muduo EventLoop)
- Singleton (ChatService, KafkaManager, CacheManager, DatabaseRouter, ConnectionPool)
- Observer pattern via callbacks (muduo connection/message callbacks)
- Read/write separation (DatabaseRouter routes writes to master, reads to slave)

## Project Structure (after cleanup)

```
chatserver/
├── src/server/
│   ├── main.cpp            — Entry point
│   ├── chatserver.cpp      — Network layer
│   ├── chatservice.cpp     — Business logic
│   ├── db/                 — db.cpp, connection_pool.cpp, database_router.cpp
│   ├── model/              — usermodel.cpp, friendmodel.cpp, groupmodel.cpp, offlinemessagemodel.cpp
│   ├── redis/              — redis_cache.cpp, cache_manager.cpp, redis_sentinel.cpp
│   ├── kafka/              — kafka_manager.cpp, kafka_producer.cpp, kafka_consumer.cpp
│   ├── log/                — async_logging.cpp, log_file.cpp
│   ├── mcp/                — chat_mcp_server.cpp (MCP tools for AI agents)
│   └── proto/              — message.proto (protobuf definitions)
├── c++_mcp/                — MCP C++ library (HTTP+SSE+Stdio transport)
├── src/client/             — Client implementation
├── include/server/         — Mirror of src/server/ headers
├── test/                   — Test code
├── docker/                 — Docker configs
├── docker-compose.yml      — Docker Compose orchestration (15 containers)
├── Dockerfile.server       — Server Docker image
├── Dockerfile.nginx        — Nginx Docker image
├── nginx.conf              — Nginx TCP load balancer config
├── CMakeLists.txt          — Build configuration
├── autobuild.sh            — Build script
├── autobuild.sh            — Build script
├── start_servers.sh        — Start 3 local servers for development
└── README.md               — Project documentation
```

## Key Implementation Details

- **Protobuf only** — JSON serialization has been fully removed. All messages use Protocol Buffers.
- **Message routing** — `ProtoMsgHandlerMap` maps Protobuf message IDs to handler callbacks via `std::function`.
- **Kafka broadcast model** — Each server instance uses a unique `group.id` (e.g. `chat_server_group_6000`), ensuring all servers receive all Kafka messages.
- **Kafka topics** — `user_messages` for private chat, `group_messages` for group chat.
- **Redis caching** — User info (30min TTL), friend lists (15min), group info (10min), user status (5min), offline message count (2min).
- **Redis Sentinel** — High-availability via sentinel auto-failover.
- **MySQL master/slave** — `DatabaseRouter` auto-routes writes to master, reads to slave. `forceMaster=true` available for read-after-write consistency.
- **Thread safety** — `_userConnMap` protected by `_connMutex` in ChatService.
- **Offline messages** — Stored in MySQL `offlinemessage` table. Pushed to user on login, then deleted.
- **MCP Server** — Optional HTTP MCP server (`--mcp-port PORT`). Exposes 8 tools: `chat_user_login`, `chat_send_message`, `chat_server_stats`, `chat_list_online_users`, `chat_get_user_info`, `chat_get_user_friends`, `chat_get_group_info`, `chat_list_user_groups`. Runs in a separate thread via `c++_mcp` library. Supports Streamable HTTP (2025-03-26 spec) on `/mcp` endpoint. `chat_user_login` validates credentials and returns user profile; `chat_send_message` sends private messages using the same delivery pipeline (local TCP → Kafka → offline storage) as the native client.

## Common Development Tasks

### Prerequisites
```bash
# Start infrastructure (MySQL, Redis, Kafka, Nginx, Sentinel)
docker compose up -d

# Build
cd build && cmake .. && make
# Or: ./autobuild.sh
```

### Start Local Servers
```bash
# Quick start 3 servers (6000/6001/6002)
./start_servers.sh
# Stop: ./start_servers.sh --kill

# Or manually (must set env vars for Kafka group ID & port):
SERVER_PORT=6000 KAFKA_HOST=localhost KAFKA_PORT=9093 nohup ./bin/ChatServer 0.0.0.0 6000 > /tmp/server0.log 2>&1 &
SERVER_PORT=6001 KAFKA_HOST=localhost KAFKA_PORT=9093 nohup ./bin/ChatServer 0.0.0.0 6001 > /tmp/server1.log 2>&1 &
SERVER_PORT=6002 KAFKA_HOST=localhost KAFKA_PORT=9093 nohup ./bin/ChatServer 0.0.0.0 6002 > /tmp/server2.log 2>&1 &
```

### Critical Environment Variables
- **`SERVER_PORT`** — MUST be set to the server's listening port (e.g. 6000). Used as Kafka group ID suffix to ensure broadcast semantics. Without this, all servers share the same group ID and cross-server messages get load-balanced instead of broadcast.
- **`KAFKA_HOST=localhost`** — Default: localhost. Set for local development.
- **`KAFKA_PORT=9093`** — Docker Kafka's EXTERNAL listener uses port 9093, not the internal 9092.
- `REDIS_SENTINEL1/2/3` — Optional: Redis Sentinel addresses for HA mode.

### Start Bridge & Frontend
```bash
# Python Bridge (already in docker compose or manually):
# cd frontend/bridge && pip install -r requirements.txt && uvicorn main:app --host 0.0.0.0 --port 8000 --reload

# Frontend (serve built dist):
cd frontend/web && npx serve dist -l 3000
```

### Run Tests
```bash
# Full functional test (requires servers running)
./test_full.sh

# Unit tests
./bin/test_db_pool
./bin/test_redis
./bin/test_models
./bin/test_kafka
```

## Key Files to Understand

- `src/server/main.cpp` — Server entry point, handler registration
- `include/server/chatserver.hpp` — Network layer (muduo)
- `include/server/chatservice.hpp` — Business logic, Kafka cross-server messaging
- `include/server/db/database_router.h` — Read/write split routing
- `include/server/redis/cache_manager.h` — Redis caching layer
- `include/server/kafka/kafka_manager.h` — Kafka cross-server messaging
- `include/server/log/async_logging.h` — Async logger
- `src/server/proto/message.proto` — Protobuf message definitions