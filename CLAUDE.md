# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a cluster chat server implemented in C++ using the Muduo network library. The system supports:
- User registration and login
- One-to-one messaging
- Group chat functionality
- Friend management
- Offline message storage
- Distributed deployment with Redis for message synchronization

## Code Architecture

### High-Level Structure
```
chatserver/
├── src/
│   ├── server/          # Server implementation
│   │   ├── db/          # Database operations (MySQL)
│   │   ├── model/       # Data models
│   │   ├── redis/       # Redis integration
│   │   └── main.cpp     # Server entry point
│   └── client/          # Client implementation
│       └── main.cpp     # Client entry point
├── include/
│   ├── server/          # Server headers
│   │   ├── db/
│   │   ├── model/
│   │   └── redis/
│   └── public.hpp       # Shared definitions
└── thirdparty/
    └── json.hpp         # JSON library
```

### Key Components

1. **ChatServer** (`chatserver.cpp`): 
   - Handles network connections using Muduo
   - Routes messages to appropriate handlers via message ID
   - Manages connection callbacks and message processing

2. **ChatService** (`chatservice.cpp`):
   - Implements business logic for all chat operations
   - Manages user connections and state with thread-safe operations
   - Integrates with Redis for distributed messaging across server instances
   - Uses singleton pattern for global access

3. **Database Layer** (`db/`):
   - MySQL wrapper for data persistence
   - Connection management and query execution
   - Uses RAII for automatic resource management

4. **Data Models** (`model/`):
   - User, Group, Friend models
   - Offline message handling
   - Group user relationships

5. **Redis Integration** (`redis/`):
   - Publish/subscribe for cross-server messaging
   - Channel management for online users
   - Separate contexts for publishing and subscribing
   - Independent thread for message observation

## Common Development Commands

### Building the Project
```bash
# Clean previous build
rm -rf build/*

# Build the project
cd build && cmake .. && make

# Or use the provided script
./autobuild.sh
```

### Running the Server
```bash
# Start server on IP and port
./bin/ChatServer 127.0.0.1 6000
```

### Running the Client
```bash
# Connect client to server
./bin/ChatClient 127.0.0.1 6000
```

## Dependencies

- Muduo network library
- MySQL client library
- Hiredis (Redis client library)
- pthread

## Key Message Flow

1. Client sends JSON message with `msgid` field
2. Server routes message to handler via `ChatService::getHandler()`
3. Handler processes business logic and responds
4. For offline users, messages are stored in database
5. For distributed deployment, Redis is used for message synchronization

## Architecture Details

### Message Handling
- Messages are JSON formatted with a `msgid` field indicating the message type
- Handlers are registered in `ChatService` constructor using `std::bind`
- Each handler function follows the signature: `void(const TcpConnectionPtr&, json&, Timestamp)`

### Thread Safety
- Connection map (`_userConnMap`) is protected with mutex (`_connMutex`)
- All access to shared data structures uses appropriate locking mechanisms

### Distributed Messaging
- Uses Redis pub/sub for cross-server communication
- Each user subscribes to a channel with their user ID when they log in
- Messages for offline users in other server instances are stored as offline messages

### Data Models
- **User**: id, name, password, state
- **Friend**: userid, friendid relationships
- **Group**: id, name, description with GroupUser members
- **Offline Messages**: Stored as strings and retrieved on login

## Database Schema
The system expects a MySQL database named `chat` with tables for users, friends, groups, group_users, and offline messages. Connection details are configured in `db.cpp`.