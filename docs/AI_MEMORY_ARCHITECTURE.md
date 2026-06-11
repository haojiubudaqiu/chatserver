# AI 智能助手 — 三层记忆架构

## 设计目标

在集群聊天服务器中，AI 智能助手需要跨会话、跨重启地保持对话记忆。传统单层存储方案（仅 MySQL 或仅内存）无法同时满足**持久性**、**上下文长度管理**和**快速恢复**三个需求。本架构通过三层分工解决：

| 需求 | 方案 | 层 |
|------|------|----|
| 会话间持久化，7天内随时恢复 | Redis db=1，7天 TTL | Layer 1 |
| 超长对话不超出 LLM 上下文窗口 | 滑动窗口 + LLM 摘要压缩 | Layer 2 |
| Agent 重启后 Redis 数据丢失时恢复 | MySQL offline message 兜底引导 | Layer 3 |

---

## 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                        AI Agent (agent_core.py)                  │
│                                                                  │
│  process_message(sender_id, sender_name, text)                   │
│       │                                                          │
│       ▼                                                          │
│  ┌───────────────────────────────────────────────────────────┐   │
│  │                    消息处理流水线                          │   │
│  │                                                           │   │
│  │  ① _load_conversation()  ←── Layer 1: Redis 加载         │   │
│  │       │                                                    │   │
│  │       ├── Redis 有数据 → 使用                               │   │
│  │       └── Redis 无数据 → ② _bootstrap_history()            │   │
│  │                              ←── Layer 3: MySQL 恢复       │   │
│  │       │                                                    │   │
│  │       ▼                                                    │   │
│  │  ③ 追加用户消息 → HumanMessage(text)                       │   │
│  │       │                                                    │   │
│  │       ▼                                                    │   │
│  │  ④ _maybe_summarize()  ←── Layer 2: 超40轮→摘要压缩       │   │
│  │       │                                                    │   │
│  │       ▼                                                    │   │
│  │  ⑤ LangGraph ainvoke() → LLM → 工具调用 → LLM              │   │
│  │       │                                                    │   │
│  │       ▼                                                    │   │
│  │  ⑥ _save_conversation()  ←── Layer 1: Redis 存储          │   │
│  │       │                                                    │   │
│  │       ▼                                                    │   │
│  │  ⑦ 返回 AI 回复文本                                        │   │
│  └───────────────────────────────────────────────────────────┘   │
│                                                                  │
│  数据通道:                                                       │
│    TCP/Protobuf ←→ ChatServer (消息收发)                         │
│    MCP HTTP     ←→ ChatServer (工具调用: 查好友/发消息/取历史)    │
│    Redis TCP    ←→ Redis db=1 (记忆持久化)                       │
│    LLM API     ←→ ModelScope (推理/摘要)                        │
└─────────────────────────────────────────────────────────────────┘
```

---

## Layer 1 — Redis 持久化

### 作用

将每次对话完整保存在 Redis 中，下次同一用户发言时加载上下文，实现跨会话记忆。

### 实现

**文件**: `agent_service/agent_core.py` 第 248-287 行  
**配置**: `agent_service/config.py` 第 36-44 行

```
数据库: Redis db=1 (与业务缓存 db=0 隔离)
Key 格式: chat:history:{user_id}
Value 格式: JSON 数组
TTL: 604800 秒 (7天)
```

Value 结构示例：

```json
[
  {"type": "human", "content": "你好，我叫张三"},
  {"type": "ai",    "content": "你好张三！我是 ChatServer AI 智能助手，有什么可以帮你的？"},
  {"type": "human", "content": "我刚才问了什么？"},
  {"type": "ai",    "content": "你刚才介绍了自己叫张三"},
  {"type": "system", "content": "[对话摘要] 用户叫张三，询问过好友列表..."}
]
```

存储逻辑（`_save_conversation`）：

```python
# 遍历最终消息列表，转成 JSON
history = [
    {
        "type": "human" if isinstance(m, HumanMessage)
                else "system" if isinstance(m, SystemMessage)
                else "ai",
        "content": m.content,
    }
    for m in messages
]
# AI 回复去掉 <think> 推理标签
if isinstance(m, AIMessage):
    content = _strip_think_tags(m.content)

# 写入 Redis，刷新 TTL
await self._redis.set(key, json.dumps(history))
await self._redis.expire(key, 604800)
```

加载逻辑（`_load_conversation`）：

```python
raw = await self._redis.get(key)
if not raw:
    return []  # Redis 无数据，触发 Layer 3

data = json.loads(raw)
messages = []
for d in data:
    if d["type"] == "human":
        messages.append(HumanMessage(content=d["content"]))
    elif d["type"] == "system":
        messages.append(SystemMessage(content=d["content"]))
    else:
        messages.append(AIMessage(content=d["content"]))
return messages
```

### 关键设计

- **AI 回复去标签**：存储时调用 `_strip_think_tags` 去掉 LLM 的 `<think>...</think>` 推理过程，只保留最终回答，节省存储空间
- **TTL 自动过期**：7 天未对话的用户自动清理，无需维护脚本
- **与业务隔离**：使用独立 db=1，不影响 db=0 的业务缓存

---

## Layer 2 — 滑动窗口 + LLM 摘要

### 作用

当单用户对话超过 40 轮（80 条消息）时，自动将最早的消息压缩为摘要，避免超出 LLM 上下文窗口，同时保留关键信息。

### 实现

**文件**: `agent_service/agent_core.py` 第 344-403 行  
**配置**:

```python
MAX_CONVERSATION_TURNS = 40   # 超过此轮数触发压缩 (80条消息)
SUMMARIZE_TURNS = 20           # 压缩时保留的最近轮数 (40条)
```

### 触发条件

```
消息总数 > MAX_CONVERSATION_TURNS × 2 = 80 条
      ↓
取前 SUMMARIZE_TURNS × 2 = 40 条 → LLM 摘要
      ↓
结果: [摘要SystemMessage, 保留的最近39条消息]
```

### 摘要生成

**首选 — LLM 摘要**（`_summarize_with_llm`，第 368-393 行）：

```python
# 拼接前 40 条消息为文本
每条消息格式: "用户: {内容}" 或 "AI: {内容}"

# 调用摘要专用 LLM (temperature=0.1)
SystemMessage: "你是一个对话摘要专家。请对以下对话进行简洁的摘要总结，
                保留所有关键信息（用户意图、重要事实、之前问过的问题等），
                用中文输出。保持摘要简洁，不超过200字。"
HumanMessage: 拼接文本
```

**降级 — 文本截取**（`_simple_summarize`，第 395-403 行）：

当无 API Key 或 LLM 调用失败时，直接拼接最近 10 条消息的摘要文本作为 fallback。

### 结果格式

```python
summary_msg = SystemMessage(content=f"[对话摘要] 此前的对话要点：\n{summary_text}")
result = [summary_msg] + recent[-keep_max:]
# keep_max = MAX_CONVERSATION_TURNS × 2 - 1 = 79
```

`[对话摘要]` 前缀让 LLM 后续推理时能区分摘要 vs 实时对话。

---

## Layer 3 — MySQL Bootstrap（重启恢复）

### 作用

当 Agent 重启导致 Redis 数据丢失时，从 MySQL `offlinemessage` 表中拉取最近的聊天记录，重建对话上下文。

### 实现

**Agent 端**: `agent_service/agent_core.py` 第 291-340 行  
**MCP 工具端**: `src/server/mcp/chat_mcp_server.cpp` 第 301-339 行

### 触发时机

```
① 用户发消息
② _load_conversation() → Redis 返回空 (key 不存在或已过期)
③ _bootstrap_history() 被调用
      ↓
通过 MCP 协议调用 chat_get_conversation_history 工具
      ↓
ChatServer 查询 MySQL offlinemessage 表
      ↓
返回按时间排序的消息列表
      ↓
还原为 HumanMessage / AIMessage
```

### MCP 工具调用

```python
# Agent 端通过 MCP SDK 调用
request = ClientRequest(
    CallToolRequest(
        params=CallToolRequestParams(
            name="chat_get_conversation_history",
            arguments={
                "user_id": user_id,          # 对话用户 ID
                "agent_id": 10000,           # AI 助手 ID
                "limit": 10,                 # 拉取条数（可配）
            },
        )
    )
)
raw = await self._mcp_session.send_request(request, LaxToolResult)
```

### 双格式兼容

MCP 工具返回值兼容两种格式（`_bootstrap_history` 第 320-325 行）：

```
旧格式 (c++_mcp 早期版本):
  raw.content = {"messages": [...], "count": 5}
  → 直接解析 dict

新格式 (MCP 标准协议):
  raw.content = [TextContent(type="text", text='{"messages":[...]}')]
  → 取 content[0].text 再 json.loads
```

### MCP 工具处理逻辑

```cpp
// C++ 端 (chat_mcp_server.cpp)
auto history = svc->getChatHistoryModel().queryPrivateChat(
    userId, agentId, limit, 0);

json messages = json::array();
for (const auto& rec : history) {
    string role = (rec.fromId == agentId) ? "assistant" : "user";
    messages.push_back({
        {"role", role},
        {"content", rec.content},
        {"time", rec.msgTime}
    });
}
std::reverse(messages.begin(), messages.end());  // 按时间正序返回
```

---

## 数据流全景

```
用户 (浏览器/客户端)
  │
  │  WebSocket / TCP
  ▼
Bridge / ChatServer
  │
  │  TCP (Protobuf)
  ▼
AI Agent (agent_core.py)
  │
  ├──────────────────── Redis (Layer 1) ──────────────────────┐
  │    存/加载对话历史 (chat:history:{user_id}, db=1, 7天TTL)  │
  │                                                            │
  ├──────────────────── LLM (Layer 2) ────────────────────────┤
  │    推理回复 + 超40轮时调用摘要模型压缩                        │
  │                                                            │
  ├──────────────────── ChatServer MCP (Layer 3) ─────────────┤
  │    Redis无数据时，通过MCP工具从MySQL拉取历史                  │
  │                                                            │
  └────────────────────────────────────────────────────────────┘
```

## 配置参数一览

| 参数 | 默认值 | 所属层 | 说明 |
|------|--------|--------|------|
| `REDIS_HOST` | `127.0.0.1` | Layer 1 | Redis 地址 |
| `REDIS_PORT` | `6379` | Layer 1 | Redis 端口 |
| `REDIS_DB` | `1` | Layer 1 | 记忆专用数据库编号 |
| `REDIS_TTL_SECONDS` | `604800` (7天) | Layer 1 | 记忆过期时间 |
| `MAX_CONVERSATION_TURNS` | `40` | Layer 2 | 超过此轮数触发摘要 |
| `SUMMARIZE_TURNS` | `20` | Layer 2 | 压缩时保留的最近轮数 |
| `BOOTSTRAP_MESSAGE_COUNT` | `10` | Layer 3 | 重启时从 MySQL 拉取条数 |
| `AI_USER_ID` | `10000` | — | AI 助手固定用户 ID |

---

## 单元测试（8 个专用测试）

**文件**: `agent_service/tests/test_agent_core.py` (146 行)

| 测试名 | 验证内容 |
|--------|----------|
| `test_agent_initialization` | Agent 初始化成功，`_app` 不为 None |
| `test_agent_process_message_basic` | 消息处理返回字符串 |
| `test_memory_isolation` | 不同 `sender_id` 记忆隔离（User1 问名字不影响 User2） |
| `test_empty_message` | 空输入不崩溃 |
| `test_redis_save_load` | Redis 存取 + 还原为正确的 LangChain 消息类型 |
| `test_maybe_summarize_short` | 短对话（10条）不触发摘要 |
| `test_maybe_summarize_trigger` | 长对话（>80条）触发压缩，结果包含 `[对话摘要]` |
| `test_bootstrap_empty_when_no_session` | 无 MCP 会话时 bootstrap 返回空列表 |

### 测试命令

```bash
# 仅执行记忆模块测试
python3 -m pytest agent_service/tests/test_agent_core.py -v

# 执行全部 14 个 Python 单元测试
python3 -m pytest agent_service/tests/ -q
```

---

## E2E 验证（10 个端到端测试）

**文件**: `test_agent_e2e.sh` (141 行)

覆盖从注册用户 → 自动添加 AI 为好友 → 给 AI 发消息 → 等待回复 → 验证回复内容 → 跨服消息 → 离线消息分页的完整链路。

```bash
./test_agent_e2e.sh
```

---

## 代码文件索引

| 文件 | 作用 |
|------|------|
| `agent_service/agent_core.py` | AI Agent 核心逻辑，包含三层记忆完整实现 |
| `agent_service/config.py` | 所有可配置参数（Redis/LLM/记忆/超时等） |
| `agent_service/tcp_bridge.py` | TCP 客户端，连接 ChatServer 收发消息 |
| `agent_service/tests/test_agent_core.py` | 记忆模块 8 个单元测试 |
| `src/server/mcp/chat_mcp_server.cpp` | `chat_get_conversation_history` MCP 工具实现 |
| `test_agent_e2e.sh` | 10 个端到端集成测试 |
