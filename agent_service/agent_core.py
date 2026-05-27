"""
LangGraph-powered AI agent with MCP tool integration.

Workflow:
  1. User sends message via TCP -> agent
  2. Agent decides: chat, search web (Tavily), or call MCP tools
  3. MCP tools invoke ChatServer's built-in tools (send message, list friends, etc.)
  4. Agent replies via TCP
"""

import os
import json
import logging
from typing import Any, Optional

from langchain_openai import ChatOpenAI
from langchain_core.messages import SystemMessage, HumanMessage, AIMessage
from langchain_core.tools import tool
from langchain_community.tools.tavily_search import TavilySearchResults
from langgraph.graph import StateGraph, START, END
from langgraph.prebuilt import ToolNode
from langgraph.checkpoint.memory import MemorySaver
from langgraph.graph.message import add_messages
from typing import TypedDict, Annotated, Sequence
from langchain_core.messages import BaseMessage

logger = logging.getLogger("agent_core")

os.environ["TAVILY_API_KEY"] = os.environ.get("TAVILY_API_KEY", "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")
os.environ["MODELSCOPE_API_KEY"] = os.environ.get("MODELSCOPE_API_KEY", "ms-5a8fdbd8-8b94-40b4-94ed-6015c5adb297")
MCP_SERVER_URL = os.environ.get("MCP_SERVER_URL", "http://localhost:8888/mcp")


class AgentState(TypedDict):
    messages: Annotated[Sequence[BaseMessage], add_messages]
    sender_id: int
    sender_name: str


class McpSession:
    """Manages an MCP Streamable HTTP session lifecycle."""

    def __init__(self, server_url: str, http_session: Any):
        self._server_url = server_url
        self._http = http_session
        self._session_id: Optional[str] = None

    async def initialize(self) -> bool:
        """Initialize MCP session: send initialize + notifications/initialized."""
        try:
            # Step 1: initialize request
            init_req = {
                "jsonrpc": "2.0",
                "id": "1",
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-03-26",
                    "capabilities": {},
                    "clientInfo": {"name": "agent-service", "version": "1.0"},
                },
            }
            async with self._http.post(
                self._server_url, json=init_req, timeout=aiohttp.ClientTimeout(total=5)
            ) as resp:
                self._session_id = resp.headers.get("Mcp-Session-Id", "")
                await resp.json()

            if not self._session_id:
                logger.error("No Mcp-Session-Id in initialize response")
                return False

            # Step 2: initialized notification
            notif = {"jsonrpc": "2.0", "method": "notifications/initialized"}
            async with self._http.post(
                self._server_url,
                json=notif,
                headers={"Mcp-Session-Id": self._session_id},
                timeout=aiohttp.ClientTimeout(total=5),
            ):
                pass

            logger.info(f"MCP session initialized: {self._session_id[:16]}...")
            return True

        except Exception as e:
            logger.error(f"MCP session initialization failed: {e}")
            return False

    async def list_tools(self) -> list:
        """List available MCP tools."""
        req = {"jsonrpc": "2.0", "id": "2", "method": "tools/list"}
        async with self._http.post(
            self._server_url,
            json=req,
            headers={"Mcp-Session-Id": self._session_id},
            timeout=aiohttp.ClientTimeout(total=5),
        ) as resp:
            result = await resp.json()
            return result.get("result", {}).get("tools", [])

    async def call_tool(self, name: str, arguments: dict) -> str:
        """Call an MCP tool and return the result text."""
        req = {
            "jsonrpc": "2.0",
            "id": "3",
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments},
        }
        async with self._http.post(
            self._server_url,
            json=req,
            headers={"Mcp-Session-Id": self._session_id},
            timeout=aiohttp.ClientTimeout(total=10),
        ) as resp:
            result = await resp.json()
            content = result.get("result", {}).get("content", [])
            parts = []
            for c in content:
                if isinstance(c, dict):
                    parts.append(c.get("text", json.dumps(c)))
                else:
                    parts.append(str(c))
            return "\n".join(parts)

    @property
    def session_id(self) -> str:
        return self._session_id or ""


class AgentCore:
    def __init__(self):
        self._app = None
        self._memory = MemorySaver()
        self._http_session: Optional[Any] = None
        self._mcp_session: Optional[McpSession] = None

    async def initialize(self):
        """Initialize LLM, MCP session, tools, and compile LangGraph workflow."""
        import aiohttp

        self._http_session = aiohttp.ClientSession()

        # Initialize MCP session
        self._mcp_session = McpSession(MCP_SERVER_URL, self._http_session)
        mcp_ok = await self._mcp_session.initialize()
        if not mcp_ok:
            logger.warning("MCP session init failed, tools will be unavailable")

        # ---------- 1. LLM via ModelScope ----------
        llm = ChatOpenAI(
            base_url="https://api-inference.modelscope.cn/v1",
            api_key=os.environ["MODELSCOPE_API_KEY"],
            model="qwen-max",
            temperature=0.3,
        )

        # ---------- 2. Tools ----------
        tools = []

        # 2a. Tavily web search
        tavily_tool = TavilySearchResults(max_results=3)
        tools.append(tavily_tool)

        # 2b. MCP tools
        if mcp_ok:
            mcp_tools_list = await self._mcp_session.list_tools()
            for t_def in mcp_tools_list:
                tools.append(self._make_mcp_tool(t_def))
                logger.info(f"  Loaded MCP tool: {t_def['name']}")
            logger.info(f"Loaded {len(mcp_tools_list)} MCP tools")
        else:
            logger.warning("No MCP tools loaded")

        llm_with_tools = llm.bind_tools(tools) if tools else llm

        # ---------- 3. Build LangGraph ----------
        def call_model(state: AgentState):
            system_prompt = (
                f"你是一个集成在高性能集群聊天服务器中的AI智能助手。\n"
                f"当前与你对话的用户ID是：{state['sender_id']}\n\n"
                f"## 可用能力\n"
                f"- 日常闲聊、问答\n"
                f"- 使用 tavily_search_results_json 搜索最新资讯（联网搜索）\n"
                f"- 调用后端MCP工具帮助用户查好友、查群组、查在线用户\n"
                f"- 使用 chat_send_message 代替用户给他的好友发送消息（from_user_id 必须使用 {state['sender_id']}）\n\n"
                f"## 行为准则\n"
                f"- 保持热情、专业、友好的语气\n"
                f"- 调用工具后，用自然语言总结结果回复用户\n"
                f"- 如果用户问你是谁，告诉用户你是ChatServer AI智能助手"
            )
            messages = [SystemMessage(content=system_prompt)] + list(state["messages"])
            response = llm_with_tools.invoke(messages)
            return {"messages": [response]}

        def should_continue(state: AgentState) -> str:
            last_msg = state["messages"][-1]
            if hasattr(last_msg, "tool_calls") and last_msg.tool_calls:
                return "tools"
            return END

        workflow = StateGraph(AgentState)
        workflow.add_node("agent", call_model)
        workflow.add_node("tools", ToolNode(tools))

        workflow.add_edge(START, "agent")
        workflow.add_conditional_edges("agent", should_continue, {"tools": "tools", END: END})
        workflow.add_edge("tools", "agent")

        self._app = workflow.compile(checkpointer=self._memory)
        logger.info(f"LangGraph agent compiled with {len(tools)} tools")

    def _make_mcp_tool(self, t_def: dict):
        """Wrap an MCP tool definition into a LangChain @tool."""
        from langchain_core.tools import tool as lc_tool

        name = t_def["name"]
        description = t_def.get("description", "")
        mcp_session = self._mcp_session

        @lc_tool
        async def dynamic_tool(**kwargs) -> str:
            """Call ChatServer MCP tool."""
            if not mcp_session:
                return "MCP session not available"
            return await mcp_session.call_tool(name, kwargs)

        dynamic_tool.name = name
        dynamic_tool.description = description
        return dynamic_tool

    async def process_message(self, sender_id: int, sender_name: str, text: str) -> str:
        """Process an incoming message through the LangGraph agent."""
        if not self._app:
            raise RuntimeError("Agent not initialized")

        config = {"configurable": {"thread_id": str(sender_id)}}
        state = {
            "messages": [HumanMessage(content=text)],
            "sender_id": sender_id,
            "sender_name": sender_name,
        }

        result = await self._app.ainvoke(state, config)
        final_response = result["messages"][-1]
        if isinstance(final_response, AIMessage):
            return final_response.content or ""
        return str(final_response)

    async def cleanup(self):
        if self._http_session:
            await self._http_session.close()
