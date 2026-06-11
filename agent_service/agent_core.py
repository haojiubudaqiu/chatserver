"""
LangGraph-powered AI agent with three-layer memory architecture.

Memory Architecture:
  Layer 1 (Redis Persistence):  Conversation history persisted in Redis (db=1)
  Layer 2 (Context Window):      Sliding window + LLM summarization for long conversations
  Layer 3 (MySQL Bootstrap):     On restart, load recent history from MySQL via MCP tool

Workflow:
  1. User sends message via TCP -> agent
  2. Agent loads history from Redis (or MySQL if missing on restart)
  3. Context window trims/summarizes long conversations
  4. Agent decides: chat, search web (Tavily), or call MCP tools
  5. MCP tools invoke ChatServer's built-in tools (send message, list friends, etc.)
  6. Agent replies via TCP
  7. Updated history saved to Redis
"""

import contextlib
import json
import logging
import openai
import re
from datetime import datetime
from typing import Any, Sequence
from uuid import uuid4

from langchain_core.messages import BaseMessage, SystemMessage, HumanMessage, AIMessage
from langchain_openai import ChatOpenAI
from langgraph.graph import StateGraph, START, END
from langgraph.graph.message import add_messages
from langgraph.prebuilt import ToolNode
from typing import TypedDict, Annotated

from mcp.client.sse import sse_client
from mcp import ClientSession
from langchain_mcp_adapters.tools import load_mcp_tools

import config

logger = logging.getLogger("agent_core")


class AgentState(TypedDict):
    messages: Annotated[Sequence[BaseMessage], add_messages]
    sender_id: int
    sender_name: str


def _make_dummy_llm():
    """Dummy LLM with unique IDs per invocation (fixes add_messages dedup)."""
    from langchain_core.language_models.chat_models import BaseChatModel
    from langchain_core.outputs import ChatResult, ChatGeneration

    class DummyChatModel(BaseChatModel):
        def _generate(self, messages, stop=None, run_manager=None, **kwargs):
            _ = messages
            msg = AIMessage(
                content="我是ChatServer AI智能助手！当前为开发模式（未配置LLM API Key）。\n"
                        "如需真实AI对话能力，请设置 MODELSCOPE_API_KEY 环境变量。\n"
                        "但我依然可以为您提供基础的聊天服务！请问有什么可以帮助您的？",
                id=str(uuid4())
            )
            return ChatResult(generations=[ChatGeneration(message=msg)])

        @property
        def _llm_type(self):
            return "dummy"

    return DummyChatModel()


def _strip_think_tags(text: str) -> str:
    return re.sub(r'<think>.*?</think>', '', text, flags=re.DOTALL).strip()


class ChatAgent:
    def __init__(self, mcp_url: str) -> None:
        self._sse_url = mcp_url.replace("/mcp", "/sse")
        self._app: Any = None
        self._model_names = [config.MODEL_NAME] + config.FALLBACK_MODELS
        self._model_idx = 0
        self._exit_stack = contextlib.AsyncExitStack()
        self._redis = None
        self._mcp_session = None
        self._tools: list = []
        self._summarize_llm = None

    def _rotate_model(self) -> None:
        for _ in range(len(self._model_names)):
            self._model_idx = (self._model_idx + 1) % len(self._model_names)
            try:
                llm = self._create_llm()
                llm.bind_tools(self._tools)
                logger.info(f"Switched to model: {self._model_names[self._model_idx]}")
                return
            except NotImplementedError:
                continue

    def _create_llm(self) -> ChatOpenAI:
        name = self._model_names[self._model_idx]
        logger.info(f"Creating LLM with model: {name}")
        return ChatOpenAI(
            base_url=config.MODELSCOPE_BASE_URL,
            api_key=config.MODELSCOPE_API_KEY,
            model=name,
            temperature=0.3,
        )

    async def initialize(self) -> None:
        import redis.asyncio as aioredis
        self._redis = aioredis.Redis(
            host=config.REDIS_HOST,
            port=config.REDIS_PORT,
            db=config.REDIS_DB,
            decode_responses=False,
        )
        try:
            await self._redis.ping()
            logger.info(f"Redis connected ({config.REDIS_HOST}:{config.REDIS_PORT}, db={config.REDIS_DB})")
        except Exception as e:
            logger.warning(f"Redis connection failed, agent will run without persistent memory: {e}")
            await self._redis.aclose()
            self._redis = None

        self._tools = []
        tools = self._tools

        if config.has_tavily_key():
            try:
                from langchain_community.tools.tavily_search import TavilySearchResults
                tools.append(TavilySearchResults(max_results=3))
                logger.info("Tavily web search enabled")
            except Exception as e:
                logger.warning(f"Failed to load Tavily: {e}")
        else:
            logger.warning("TAVILY_API_KEY not set, web search disabled")

        try:
            streams = await self._exit_stack.enter_async_context(sse_client(self._sse_url))
            session = await self._exit_stack.enter_async_context(ClientSession(*streams))
            await session.initialize()
            mcp_tools = await load_mcp_tools(session)
            tools.extend(mcp_tools)
            self._mcp_session = session
            logger.info(f"Loaded {len(mcp_tools)} MCP tools via Official SDK")
        except Exception as e:
            logger.error(f"Failed to load MCP tools via SDK: {e}")

        if config.has_model_api_key():
            llm_with_tools = None
            for _ in range(len(self._model_names)):
                llm = self._create_llm()
                try:
                    llm_with_tools = llm.bind_tools(tools)
                    break
                except NotImplementedError:
                    logger.warning(f"Model {self._model_names[self._model_idx]} does not support tool binding")
                    self._model_idx = (self._model_idx + 1) % len(self._model_names)
            if llm_with_tools is None:
                logger.warning("No LLM model supports tool binding, running without tools")
                llm_with_tools = self._create_llm()

            self._summarize_llm = ChatOpenAI(
                base_url=config.MODELSCOPE_BASE_URL,
                api_key=config.MODELSCOPE_API_KEY,
                model=self._model_names[0],
                temperature=0.1,
            )
        else:
            logger.warning("MODELSCOPE_API_KEY not set, using a dummy LLM")
            llm_with_tools = _make_dummy_llm()

        def call_model(state: AgentState) -> dict:
            nonlocal llm_with_tools
            now = datetime.now()
            system = (
                f"你是一个集成在高性能集群聊天服务器中的AI智能助手。\n"
                f"当前日期时间：{now.strftime('%Y年%m月%d日 %H:%M')}\n"
                f"当前与你对话的用户ID是：{state['sender_id']}（{state.get('sender_name','')}）\n\n"
                f"## 可用能力\n"
                f"- 日常闲聊、问答\n"
                f"- 使用 tavily_search_results_json 搜索最新资讯（联网搜索）\n"
                f"  - 当用户问'今天'、'昨天'、'最近'等涉及当前日期的问题时，必须使用 tavily 搜索获取实时信息\n"
                f"  - 搜索时在关键词中主动加上当前日期（{now.strftime('%Y年%m月%d日')}）以提高准确性\n"
                f"- 调用后端MCP工具查好友、查群组、查在线用户、查看服务器统计\n"
                f"- 使用 chat_send_message 帮用户给他的好友发消息（from_user_id 必须用 {state['sender_id']}）\n\n"
                f"## 行为准则\n"
                f"- 热情、专业、友好\n"
                f"- 如果用户一次问了多个问题，必须逐一回答，每个问题调用对应的工具\n"
                f"- 调用工具后用自然语言总结结果回复用户\n"
                f"- 如果工具调用返回 success=true，**必须在回复中明确告知用户操作成功**，不得说失败或遇到问题\n"
                f"- 每个工具只调用一次，不要重复调用同一工具\n"
                f"- 用户问你是谁，回答：ChatServer AI智能助手"
            )
            msgs = [SystemMessage(content=system)] + list(state["messages"])
            for attempt in range(3):
                try:
                    response = llm_with_tools.invoke(msgs)
                    if hasattr(response, "content") and response.content is None:
                        logger.warning(f"LLM returned null content (attempt {attempt+1}), retrying...")
                        continue
                    return {"messages": [response]}
                except openai.RateLimitError:
                    logger.warning(f"Model {self._model_names[self._model_idx]} quota exceeded")
                    self._rotate_model()
                    llm = self._create_llm()
                    llm_with_tools = llm.bind_tools(tools)
                    continue
                except Exception as e:
                    logger.warning(f"Model {self._model_names[self._model_idx]} failed (attempt {attempt+1}): {e}")
                    if attempt < 2:
                        self._rotate_model()
                        llm = self._create_llm()
                        llm_with_tools = llm.bind_tools(tools)
                        continue
                    raise

        def should_continue(state: AgentState) -> str:
            last = state["messages"][-1]
            if hasattr(last, "tool_calls") and last.tool_calls:
                return "tools"
            return END

        workflow = StateGraph(AgentState)
        workflow.add_node("agent", call_model)
        workflow.add_node("tools", ToolNode(tools))
        workflow.add_edge(START, "agent")
        workflow.add_conditional_edges(
            "agent", should_continue, {"tools": "tools", END: END}
        )
        workflow.add_edge("tools", "agent")

        self._app = workflow.compile()
        logger.info(f"LangGraph agent compiled with {len(tools)} tools")

    async def close(self) -> None:
        await self._exit_stack.aclose()
        if self._redis:
            try:
                await self._redis.aclose()
            except Exception:
                pass
        logger.info("Agent connections closed.")

    # ── Memory Layer 1: Redis Persistence ───────────────────────────

    async def _save_conversation(self, sender_id: int, messages: list) -> None:
        if not self._redis:
            return
        key = f"chat:history:{sender_id}"
        history = [
            {
                "type": "human" if isinstance(m, HumanMessage)
                        else "system" if isinstance(m, SystemMessage)
                        else "ai",
                "content": _strip_think_tags(m.content) if isinstance(m, AIMessage) else m.content,
            }
            for m in messages if isinstance(m, (HumanMessage, AIMessage, SystemMessage))
        ]
        try:
            await self._redis.set(key, json.dumps(history))
            await self._redis.expire(key, config.REDIS_TTL_SECONDS)
        except Exception as e:
            logger.warning(f"Failed to save conversation for user {sender_id}: {e}")

    async def _load_conversation(self, sender_id: int) -> list:
        if not self._redis:
            return []
        key = f"chat:history:{sender_id}"
        try:
            raw = await self._redis.get(key)
            if not raw:
                return []
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
        except Exception as e:
            logger.warning(f"Failed to load conversation for user {sender_id}: {e}")
            return []

    # ── Memory Layer 3: MySQL Bootstrap (on restart) ────────────────

    async def _bootstrap_history(self, user_id: int) -> list:
        """Load recent conversation from MySQL via MCP tool when Redis has no data."""
        if not self._mcp_session:
            return []
        try:
            from pydantic import BaseModel
            from typing import Any
            from mcp.types import (
                ClientRequest, CallToolRequest, CallToolRequestParams,
            )

            class LaxToolResult(BaseModel):
                content: Any = None
                isError: bool = False

            request = ClientRequest(
                CallToolRequest(
                    params=CallToolRequestParams(
                        name="chat_get_conversation_history",
                        arguments={
                            "user_id": user_id,
                            "agent_id": config.AI_USER_ID,
                            "limit": config.BOOTSTRAP_MESSAGE_COUNT,
                        },
                    )
                )
            )
            raw = await self._mcp_session.send_request(request, LaxToolResult)
            data = {}
            if isinstance(raw.content, dict):
                data = raw.content
            elif isinstance(raw.content, list) and len(raw.content) > 0:
                ct = raw.content[0]
                if hasattr(ct, "text") and ct.text:
                    data = json.loads(ct.text)
            if "error" in data:
                logger.warning(f"Bootstrap failed for user {user_id}: {data['error']}")
                return []
            messages = []
            for msg in data.get("messages", []):
                role = msg.get("role", "user")
                if role == "user":
                    messages.append(HumanMessage(content=msg["content"]))
                else:
                    messages.append(AIMessage(content=msg["content"]))
            logger.info(f"Bootstrapped {len(messages)} messages for user {user_id} from MySQL")
            return messages
        except Exception as e:
            logger.warning(f"Bootstrap via MCP failed for user {user_id}: {e}")
        return []

    # ── Memory Layer 2: Context Window + LLM Summarization ──────────

    async def _maybe_summarize(self, messages: list) -> list:
        """If conversation exceeds max turns, summarize oldest messages using LLM."""
        max_msgs = config.MAX_CONVERSATION_TURNS * 2
        if len(messages) <= max_msgs:
            return messages

        summarize_count = config.SUMMARIZE_TURNS * 2
        to_summarize = messages[:summarize_count]
        recent = messages[summarize_count:]

        summary_text = await self._summarize_with_llm(to_summarize)
        if not summary_text:
            summary_text = self._simple_summarize(to_summarize)

        summary_msg = SystemMessage(content=f"[对话摘要] 此前的对话要点：\n{summary_text}")

        keep_max = max_msgs - 1
        if len(recent) > keep_max:
            recent = recent[-keep_max:]

        result = [summary_msg] + recent
        logger.info(f"Conversation summarized: {len(messages)} -> {len(result)} messages")
        return result

    async def _summarize_with_llm(self, messages: list) -> str:
        """Use LLM to generate a concise summary of the given messages."""
        if not self._summarize_llm or not config.has_model_api_key():
            return ""
        lines = []
        for m in messages:
            if isinstance(m, HumanMessage):
                lines.append(f"用户: {m.content[:200]}")
            elif isinstance(m, AIMessage):
                lines.append(f"AI: {m.content[:200]}")
        text = "\n".join(lines)

        try:
            resp = self._summarize_llm.invoke([
                SystemMessage(
                    content="你是一个对话摘要专家。请对以下对话进行简洁的摘要总结，"
                            "保留所有关键信息（用户意图、重要事实、之前问过的问题等），"
                            "用中文输出。保持摘要简洁，不超过200字。"
                ),
                HumanMessage(content=text),
            ])
            if resp and resp.content:
                return resp.content.strip()
        except Exception as e:
            logger.warning(f"LLM summarization failed: {e}")
        return ""

    def _simple_summarize(self, messages: list) -> str:
        """Fallback: simple text-based summarization without LLM."""
        lines = []
        for m in messages[-10:]:
            if isinstance(m, HumanMessage):
                lines.append(f"用户: {m.content[:100]}")
            elif isinstance(m, AIMessage):
                lines.append(f"AI: {m.content[:100]}")
        return "；".join(lines)

    # ── Main message processing ─────────────────────────────────────

    async def process_message(
        self, sender_id: int, sender_name: str, text: str
    ) -> str:
        if not self._app:
            return "Agent not initialized."

        # Layer 1: Load existing conversation from Redis
        messages = await self._load_conversation(sender_id)

        # Layer 3: If Redis has no data, bootstrap from MySQL (handles restart)
        if not messages:
            history = await self._bootstrap_history(sender_id)
            if history:
                messages = history

        # Add current user message
        messages.append(HumanMessage(content=text))

        # Layer 2: Apply context window (summarize oldest messages if too long)
        messages = await self._maybe_summarize(messages)

        # Run LangGraph (no checkpointer -- state is managed externally via Redis)
        state = {
            "messages": messages,
            "sender_id": sender_id,
            "sender_name": sender_name,
        }
        config_dict = {
            "configurable": {"thread_id": str(sender_id)},
            "recursion_limit": 12,
        }

        try:
            result = await self._app.ainvoke(state, config_dict)
        except Exception as e:
            logger.error(f"LangGraph ainvoke failed: {e}", exc_info=True)
            return "抱歉，AI处理请求时遇到暂时性错误，请稍后再试。"

        # Save updated conversation to Redis
        final_messages = list(result["messages"])
        await self._save_conversation(sender_id, final_messages)

        # Extract and return the AI reply
        for msg in reversed(final_messages):
            if isinstance(msg, AIMessage) and msg.content:
                cleaned = _strip_think_tags(msg.content)
                if cleaned:
                    logger.info(f"Reply to {sender_name}: {cleaned[:80]}")
                    return cleaned
                logger.info(f"Reply to {sender_name} (after strip): {msg.content[:80]}")
                return msg.content

        final = final_messages[-1]
        if isinstance(final, AIMessage):
            return final.content or "（空回复）"

        logger.warning(f"Unexpected final msg type: {type(final).__name__}")
        return "抱歉，我暂时无法回复，请稍后再试。"
