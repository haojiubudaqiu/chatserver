"""
LangGraph-powered AI agent with MCP tool integration.

Workflow:
  1. User sends message via TCP -> agent
  2. Agent decides: chat, search web (Tavily), or call MCP tools
  3. MCP tools invoke ChatServer's built-in tools (send message, list friends, etc.)
  4. Agent replies via TCP
"""

import aiohttp
import json
import logging
from typing import Any, Optional, Sequence
from uuid import uuid4

from langchain_core.messages import BaseMessage, SystemMessage, HumanMessage, AIMessage
from langchain_openai import ChatOpenAI
from langgraph.graph import StateGraph, START, END
from langgraph.graph.message import add_messages
from langgraph.prebuilt import ToolNode
from langgraph.checkpoint.memory import MemorySaver
from typing import TypedDict, Annotated

import config

logger = logging.getLogger("agent_core")


class AgentState(TypedDict):
    messages: Annotated[Sequence[BaseMessage], add_messages]
    sender_id: int
    sender_name: str


class McpSession:
    """Short-lived MCP session for a single tool call."""

    def __init__(self, server_url: str) -> None:
        self._server_url = server_url
        self._session_id: Optional[str] = None

    async def _request(self, body: dict, timeout: int = 10) -> Any:
        import aiohttp

        headers = {}
        if self._session_id:
            headers["Mcp-Session-Id"] = self._session_id

        async with aiohttp.ClientSession() as session:
            async with session.post(
                self._server_url,
                json=body,
                headers=headers,
                timeout=aiohttp.ClientTimeout(total=timeout),
            ) as resp:
                resp.raise_for_status()
                data = await resp.json()

                if self._session_id is None:
                    sid = resp.headers.get("Mcp-Session-Id")
                    if sid:
                        self._session_id = sid

                if "error" in data:
                    raise RuntimeError(
                        f"MCP error (code={data['error'].get('code')}): "
                        f"{data['error'].get('message')}"
                    )

                return data.get("result")

    async def initialize(self) -> None:
        result = await self._request(
            {
                "jsonrpc": "2.0",
                "id": "1",
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-03-26",
                    "capabilities": {},
                    "clientInfo": {"name": "agent-service", "version": "1.0"},
                },
            }
        )
        if result is None:
            raise RuntimeError("MCP initialize returned no result")

        async with aiohttp.ClientSession() as session:
            await session.post(
                self._server_url,
                json={"jsonrpc": "2.0", "method": "notifications/initialized"},
                headers={"Mcp-Session-Id": self._session_id},
                timeout=aiohttp.ClientTimeout(total=5),
            )

    async def list_tools(self) -> list:
        result = await self._request(
            {"jsonrpc": "2.0", "id": "2", "method": "tools/list"}
        )
        return (result or {}).get("tools", [])

    async def call_tool(self, name: str, arguments: dict) -> str:
        result = await self._request(
            {
                "jsonrpc": "2.0",
                "id": "3",
                "method": "tools/call",
                "params": {"name": name, "arguments": arguments},
            }
        )
        if result is None:
            return ""
        content = result.get("content")
        if content is None:
            return ""
        if isinstance(content, dict):
            return content.get("text", json.dumps(content, ensure_ascii=False))
        if isinstance(content, list):
            parts = []
            for c in content:
                if isinstance(c, dict):
                    parts.append(c.get("text", json.dumps(c, ensure_ascii=False)))
                else:
                    parts.append(str(c))
            return "\n".join(parts)
        return str(content)


class McpToolWrapper:
    def __init__(self, server_url: str, name: str, description: str) -> None:
        self._server_url = server_url
        self.name = name
        self.description = description

    async def arun(self, **kwargs: Any) -> str:
        session = McpSession(self._server_url)
        try:
            await session.initialize()
            return await session.call_tool(self.name, kwargs)
        except Exception as e:
            logger.warning(f"MCP tool '{self.name}' failed: {e}")
            return f"Error: {e}"


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


class ChatAgent:
    def __init__(self, mcp_url: str) -> None:
        self._mcp_url = mcp_url
        self._app: Any = None
        self._memory = MemorySaver()

    async def initialize(self) -> None:
        tools = []

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
            init_session = McpSession(self._mcp_url)
            await init_session.initialize()
            tool_defs = await init_session.list_tools()

            for td in tool_defs:
                wrapper = McpToolWrapper(
                    self._mcp_url, td["name"], td.get("description", "")
                )

                from langchain_core.tools import StructuredTool

                def make_arun_wrapper(w: McpToolWrapper):
                    async def mcp_fn(**kwargs: Any) -> str:
                        return await w.arun(**kwargs)
                    return mcp_fn

                mcp_tool = StructuredTool.from_function(
                    coroutine=make_arun_wrapper(wrapper),
                    name=td["name"],
                    description=td.get("description", "MCP Tool"),
                )

                tools.append(mcp_tool)
                logger.info(f"  Loaded MCP tool: {td['name']}")

            logger.info(f"Loaded {len(tool_defs)} MCP tools")

        except Exception as e:
            logger.warning(f"Failed to load MCP tools: {e}")

        if config.has_model_api_key():
            llm = ChatOpenAI(
                base_url=config.MODELSCOPE_BASE_URL,
                api_key=config.MODELSCOPE_API_KEY,
                model=config.MODEL_NAME,
                temperature=0.3,
            )
        else:
            logger.warning("MODELSCOPE_API_KEY not set, using a dummy LLM")
            llm = _make_dummy_llm()

        if tools:
            try:
                llm_with_tools = llm.bind_tools(tools)
            except NotImplementedError:
                logger.warning("LLM does not support tool binding, running without tools")
                llm_with_tools = llm
        else:
            llm_with_tools = llm

        def call_model(state: AgentState) -> dict:
            system = (
                f"你是一个集成在高性能集群聊天服务器中的AI智能助手。\n"
                f"当前与你对话的用户ID是：{state['sender_id']}（{state.get('sender_name','')}）\n\n"
                f"## 可用能力\n"
                f"- 日常闲聊、问答\n"
                f"- 使用 tavily_search_results_json 搜索最新资讯（联网搜索）\n"
                f"- 调用后端MCP工具查好友、查群组、查在线用户、查看服务器统计\n"
                f"- 使用 chat_send_message 帮用户给他的好友发消息（from_user_id 必须用 {state['sender_id']}）\n\n"
                f"## 行为准则\n"
                f"- 热情、专业、友好\n"
                f"- 调用工具后用自然语言总结结果回复用户\n"
                f"- 用户问你是谁，回答：ChatServer AI智能助手"
            )
            msgs = [SystemMessage(content=system)] + list(state["messages"])
            response = llm_with_tools.invoke(msgs)
            return {"messages": [response]}

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

    async def process_message(
        self, sender_id: int, sender_name: str, text: str
    ) -> str:
        if not self._app:
            return "Agent not initialized."

        config_dict = {"configurable": {"thread_id": str(sender_id)}}
        state = {
            "messages": [HumanMessage(content=text)],
            "sender_id": sender_id,
            "sender_name": sender_name,
        }

        result = await self._app.ainvoke(state, config_dict)

        for msg in reversed(result["messages"]):
            if isinstance(msg, AIMessage) and msg.content:
                logger.info(f"Reply to {sender_name}: {msg.content[:80]}")
                return msg.content

        final = result["messages"][-1]
        if isinstance(final, AIMessage):
            return final.content or "（空回复）"

        logger.warning(f"Unexpected final msg type: {type(final).__name__}")
        return "抱歉，我暂时无法回复，请稍后再试。"
