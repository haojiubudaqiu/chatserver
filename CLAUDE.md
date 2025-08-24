# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a cluster chat server developed based on the muduo network library for Linux environments. It implements features such as user registration, login, adding friends, creating/joining groups, private messaging, group chat, and offline message storage.

## Build System

The project uses CMake as its build system with the following structure:
- Main CMakeLists.txt in the root directory
- Separate CMakeLists.txt for server, client, and test components
- Executables are output to the bin directory

### Build Commands

```bash
# Standard build process
cd build
cmake ..
make

# Or use the automated build script
./autobuild.sh
```

### Dependencies

The project depends on:
- muduo network library (muduo_net, muduo_base)
- MySQL client library (mysqlclient)
- Redis client library (hiredis)
- Protobuf library (protobuf)
- pthread library

Header paths are configured for:
- MySQL: /usr/include/mysql
- Redis: /usr/include/hiredis
- muduo: /usr/local/lib

## Architecture

The project follows a layered architecture:

### Server Components
1. **ChatServer** - Main server class that handles network connections using muduo
2. **ChatService** - Business logic layer implementing chat features (singleton pattern)
3. **Data Models** - ORM classes for database entities:
   - User, Friend, Group, OfflineMessage
4. **Database Layer** - MySQL wrapper for data persistence
5. **Redis Layer** - Redis wrapper for message queuing between server instances
6. **Protobuf** - Message serialization using Google's Protocol Buffers

### Client Components
1. **ChatClient** - Simple client implementation for testing

### Key Patterns
- Single-threaded reactor pattern using muduo
- Singleton pattern for ChatService
- Observer pattern for connection callbacks
- Separate model classes for data access
- Dual message handling (JSON and Protobuf)

## Project Structure

```
chatserver/
├── build/           # Build directory
├── bin/             # Compiled executables
├── src/             # Source code
│   ├── server/      # Server implementation
│   │   ├── db/      # Database operations
│   │   ├── model/   # Data models
│   │   ├── proto/   # Protobuf definitions
│   │   └── redis/   # Redis operations
│   └── client/      # Client implementation
├── include/         # Header files
│   ├── server/      # Server headers
│   │   ├── db/      # Database headers
│   │   ├── model/   # Model headers
│   │   ├── proto/   # Protobuf generated headers
│   │   └── redis/   # Redis headers
│   └── client/      # Client headers
└── test/            # Test code
```

## Development Workflow

1. Make changes to source files in src/ and headers in include/
2. Navigate to build directory
3. Run cmake .. to configure the build
4. Run make to compile
5. Executables will be in the bin/ directory

## Key Implementation Details

- Uses JSON and Protobuf for message serialization
- Implements message handlers as std::function callbacks
- Uses MySQL for persistent storage
- Uses Redis for inter-server communication in cluster mode
- Thread-safe connection management with mutex protection
- Supports both JSON and Protobuf message formats

## System Architecture Details

The chat server is built with a modular architecture consisting of four main components:

1. **Network Module**: Built on the open-source muduo network library, this design decouples the network layer from business logic, allowing developers to focus on core features.

2. **Service Layer**: Utilizes C++11 features such as maps and binders to create message ID to callback bindings. When network I/O produces a message request, the system parses JSON/Protobuf from the request to extract the message ID and processes the message through the corresponding callback.

3. **Data Storage Layer**: Uses MySQL to persist critical data including user accounts, offline messages, friend lists, and group relationships.

4. **Cluster Communication Layer**: For single-server deployments, the above modules are sufficient. However, to support multi-server scaling, the system uses Redis as a message queue with its publish/subscribe functionality to enable cross-server message communication. Multiple network servers can be deployed behind an Nginx load balancer for enhanced scalability.

## Common Development Tasks

### Building the Project
```bash
# Clean build
cd build && rm -rf * && cmake .. && make

# Or use the automated script
./autobuild.sh
```

### Running the Server
```bash
# Start server on localhost:6000
./bin/ChatServer 127.0.0.1 6000
```

### Running Tests
```bash
# Run specific tests
./bin/protobuf_test
./test/testJson/testJson
```

### Key Files to Understand the Codebase
- `src/server/main.cpp` - Entry point for the server
- `include/server/chatserver.hpp` - Network layer implementation
- `include/server/chatservice.hpp` - Business logic layer
- `include/server/model/*.hpp` - Data models
- `include/server/db/db.h` - Database wrapper
- `include/server/redis/redis.hpp` - Redis wrapper
- `src/server/proto/message.proto` - Protobuf message definitions

## Upgrade Roadmap

The project has plans for significant upgrades including:
1. Migration from JSON to Protobuf for message serialization
2. Replacement of Redis with Kafka for message queuing
3. Implementation of Redis caching layer for improved performance
4. MySQL database improvements with connection pooling and replication
5. Nginx load balancing configuration
6. Asynchronous logging system implementation

See UPGRADE_PLAN.md and REDIS_CACHING_DESIGN.md for detailed implementation plans.