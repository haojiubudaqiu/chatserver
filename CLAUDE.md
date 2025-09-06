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

我们来极其详细地解读这份 CLAUDE.md 文档。这份文档是一个项目导航手册，它的核心目的不是给人看的，而是给 Claude AI 助手看的。它帮助 Claude 快速理解这个代码仓库的结构、技术栈、构建方式和设计理念，从而能更智能地协助开发者进行编码、调试和重构。

我将分部分进行超详细的解读。

一、文档属性和目的
文件名：CLAUDE.md

核心受众：Claude Code (claude.ai/code) 这个AI编程助手。

核心目的：充当项目的“入职培训文档”和“开发手册”，让AI在分析代码、回答问题、生成代码时，能基于准确的上下文信息，提供更相关、更正确的建议。

通俗理解：就像你新加入一个公司，会有一份文档告诉你公司架构、项目流程、找谁沟通一样。CLAUDE.md 就是AI助手的“入职指南”，让它能快速融入项目，高效地为你服务。

二、项目概述解读
markdown
This is a cluster chat server developed based on the muduo network library for Linux environments. It implements features such as user registration, login, adding friends, creating/joining groups, private messaging, group chat, and offline message storage.
项目类型：cluster chat server（集群聊天服务器）。关键词是“集群”，意味着它不是一个单点应用，而是可以被部署成多个实例，通过负载均衡共同工作。

核心技术栈：muduo network library。这是一个非常著名的、基于 Reactor 模式的现代 C++ 网络库，由陈硕大神开发，以高性能和简洁性著称，是开发Linux下C++网络服务的绝佳选择。

核心功能：列出了IM（即时通讯）的核心功能，从基础的注册登录，到社交关系（好友/群组），再到核心业务（私聊/群聊/离线消息）。这为AI勾勒出了业务边界。

三、构建系统详解
这部分是AI（和开发者）如何编译这个项目的说明书。

1. 构建工具：CMake
是什么：一个跨平台的自动化构建系统生成器。它比直接写Makefile更现代、更简单。

结构：

Main CMakeLists.txt：根目录的总配置文件，定义全局设置和添加子目录。

Separate CMakeLists.txt：在 src/server, src/client, test 等子目录中，管理局部的编译规则。

2. 构建命令
给出了两种标准构建方式：

手动标准流程：

bash
cd build   # 进入专门构建目录（源代码外构建，好习惯）
cmake ..   # 让CMake读取上一级目录的CMakeLists.txt，生成Makefile
make       # 执行编译
自动化脚本：./autobuild.sh。这种脚本通常包含了清理旧构建、执行cmake和make、甚至运行一些基础测试的命令，一键完成，提升效率。

3. 依赖库
这是最关键的信息之一，它告诉AI（和开发者）需要提前安装哪些第三方库才能成功编译项目。

muduo_net, muduo_base：核心网络库。

mysqlclient：MySQL数据库的C语言客户端库，用于连接和操作MySQL。

hiredis：Redis数据库的C语言客户端库，以轻量和高效闻名。

protobuf：Google的Protocol Buffers，用于高效的二进制数据序列化。

pthread：POSIX线程库，用于多线程编程。

头文件路径：还贴心地给出了这些库的默认安装路径，帮助AI和开发者解决常见的“找不到头文件”的编译错误。

四、架构解析
这部分是项目的“技术蓝图”，解释了代码是如何组织的。

1. 服务器组件（分层架构）
这是一个非常清晰的分层设计，是现代软件工程的典范：

ChatServer：网络层。基于muduo，只负责最底层的网络I/O、连接管理、数据收发。它不关心数据是什么。

ChatService：业务逻辑层（服务层）。这是项目的“大脑”和“指挥中心”。它接收来自网络层的原始数据，解析出是什么业务请求（如“登录”、“发消息”），然后调用相应的“处理函数”来完成业务逻辑。它使用单例模式，因为整个服务器只需要一个业务逻辑中心。

Data Models：数据模型层（ORM）。是业务逻辑层与数据库之间的“翻译官”。它将数据库中的表（如user表）映射成C++的类（如User类），业务逻辑层操作这些对象，模型层负责将这些操作转换成SQL语句并执行。

Database Layer：数据持久层。对MySQL客户端库的封装，提供更易用的接口。

Redis Layer：集群通信层。对Redis客户端库的封装，用于多个聊天服务器实例之间的消息转发。

Protobuf：数据序列化层。定义了一种高效、跨语言的“通用数据格式”，用于网络传输和存储。

2. 关键设计模式
Reactor模式：muduo库的核心模式。简单说就是“事件驱动”：一个主线程（EventLoop）负责监听所有网络事件（如新连接、数据可读），当事件发生时，它通知相应的处理函数去处理。这是高性能网络服务器的标配。

Singleton模式：确保一个类只有一个实例。ChatService 是单例，因为业务处理器全局一份就够了。

Observer模式：muduo中大量使用回调函数（callbacks），这本质上是观察者模式。你向EventLoop注册一个“对读事件感兴趣”的回调，当读事件发生时，它就会通知你。

五、项目结构详解
这部分是项目的“文件夹地图”，告诉AI代码文件都放在哪里。

text
chatserver/
├── build/           # 构建目录（编译时生成，不要提交到git）
├── bin/             # 输出目录（编译后的可执行文件）
├── src/             # 源代码
│   ├── server/      # 服务器代码
│   │   ├── db/      # 数据库操作（SQL执行）
│   │   ├── model/   # 数据模型（ORM对象）
│   │   ├── proto/   # Protobuf定义文件(.proto)
│   │   └── redis/   # Redis操作
│   └── client/      # 客户端代码（用于测试）
├── include/         # 头文件
│   ├── server/      # 服务器头文件
│   │   ├── db/      # 
│   │   ├── model/   # （与src目录结构镜像）
│   │   ├── proto/   # Protobuf生成的头文件
│   │   └── redis/   # 
│   └── client/      # 客户端头文件
└── test/            # 测试代码
重要提示：include 和 src 的目录结构是镜像的。这是一种常见的C++项目组织方式，include 存放公开的接口（.hpp文件），src 存放具体的实现（.cpp文件）。

六、开发工作流与关键文件
这部分是“操作手册”，告诉AI（和新开发者）日常开发应该怎么做。

流程：改代码 -> 进入build目录 -> cmake .. -> make -> 在bin/目录找可执行文件。

关键文件列表：这是给AI的“重点学习资料”。AI在理解项目时，会被引导先去阅读这些核心文件：

main.cpp：程序入口，看看服务器是怎么启动的。

chatserver.hpp：网络层是如何配置和初始化的。

chatservice.hpp：业务的“总调度中心”，所有消息处理函数都在这里注册。

model/*.hpp：数据是如何定义的。

db.h / redis.hpp：如何连接和操作数据库/缓存。

message.proto：网络协议的宪法。所有在网络上传收的数据结构都在这里定义。这是理解整个系统通信的基础。

七、系统架构细节（进阶解读）
这部分是对“架构”部分的深化，解释了“集群”是如何工作的。

网络模块：再次强调muduo解耦了网络和业务，这是良好设计的第一步。

服务层：详细解释了消息路由机制。用 std::function 和 std::map 建立了一个“消息ID -> 处理函数”的映射表。收到消息后，根据ID查找并调用对应的函数，非常灵活和高效。

数据存储：MySQL负责所有需要永久保存的数据。

集群通信：这是实现多服务器的关键。

问题：用户A连接在服务器实例1上，用户B连接在服务器实例2上。A给B发消息，消息到达了服务器1，但服务器1上没有用户B的连接，怎么发？

解决方案：利用Redis的发布订阅(Pub/Sub) 功能。

服务器1收到A给B的消息后，发现自己没有B，于是它向Redis的一个特定频道（比如 user:B）发布这条消息。

所有服务器实例都会订阅一个全局频道，监听是不是有发给自家用户的消息。

服务器2订阅了Redis，它收到消息后发现是发给B的，而B正好连接在自己这里，于是就把消息转发给B的客户端。

负载均衡：用户通过Nginx（反向代理/负载均衡器）连接进来，Nginx负责把用户连接均匀地分发到后端的多个聊天服务器实例上。

八、升级路线图
这指出了项目未来的演进方向，让AI知道哪些代码可能是“过渡性质”的（如JSON序列化），以及未来可能的重构点（如用Kafka替换Redis）。

总结
这份 CLAUDE.md 文档是一个极其专业和高效的工程实践。它不仅仅是一份说明，更是一个力导向图，引导AI（以及任何阅读它的人）快速地抓住项目的核心脉络，从宏观架构到微观实现，从开发流程到未来规划，都有了清晰的认识。它极大地降低了项目的理解和维护成本。

