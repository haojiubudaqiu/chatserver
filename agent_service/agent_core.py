"""
LangGraph-powered AI agent with MCP tool integration.

Workflow:
  1. User sends message via TCP -> agent
  2. Agent decides: chat, search web (Tavily), or call MCP tools
  3. MCP tools invoke ChatServer's built-in tools (send message, list friends, etc.)
  4. Agent replies via TCP
"""

import contextlib
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
from langgraph.checkpoint.memory import MemorySaver
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
        self._memory = MemorySaver()
        # Models to try in order; rotates on 429 (quota exceeded)
        self._model_names = [config.MODEL_NAME] + config.FALLBACK_MODELS
        self._model_idx = 0
        self._exit_stack = contextlib.AsyncExitStack()

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
        self._tools: list = []
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
            logger.info(f"Loaded {len(mcp_tools)} MCP tools via Official SDK")

        except Exception as e:
            logger.error(f"Failed to load MCP tools via SDK: {e}")

        if config.has_model_api_key():
            llm_with_tools = None
            # Try models in order; if bind_tools fails, cycle to next
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
                f"- 用户问你是谁，回答：ChatServer AI智能助手"
            )
            msgs = [SystemMessage(content=system)] + list(state["messages"])
            for attempt in range(3):
                try:
                    response = llm_with_tools.invoke(msgs)
                    # Some models return null `choices` on transient errors
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

        self._app = workflow.compile(checkpointer=self._memory)
        logger.info(f"LangGraph agent compiled with {len(tools)} tools")

    async def close(self) -> None:
        """优雅关闭 Agent 服务，断开 SSE 长连接"""
        await self._exit_stack.aclose()
        logger.info("MCP SSE connections closed.")

    async def process_message(
        self, sender_id: int, sender_name: str, text: str
    ) -> str:
        if not self._app:
            return "Agent not initialized."

        config_dict = {"configurable": {"thread_id": str(sender_id)}, "recursion_limit": 12}
        state = {
            "messages": [HumanMessage(content=text)],
            "sender_id": sender_id,
            "sender_name": sender_name,
        }

        try:
            result = await self._app.ainvoke(state, config_dict)
        except Exception as e:
            logger.error(f"LangGraph ainvoke failed: {e}", exc_info=True)
            return f"抱歉，AI处理请求时遇到暂时性错误，请稍后再试。"

        for msg in reversed(result["messages"]):
            if isinstance(msg, AIMessage) and msg.content:
                cleaned = _strip_think_tags(msg.content)
                if cleaned:
                    logger.info(f"Reply to {sender_name}: {cleaned[:80]}")
                    return cleaned
                logger.info(f"Reply to {sender_name} (after strip): {msg.content[:80]}")
                return msg.content

        final = result["messages"][-1]
        if isinstance(final, AIMessage):
            return final.content or "（空回复）"

        logger.warning(f"Unexpected final msg type: {type(final).__name__}")
        return "抱歉，我暂时无法回复，请稍后再试。"
