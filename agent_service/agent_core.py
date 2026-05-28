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
import openai
import re
from typing import Any, Optional, Sequence
from uuid import uuid4

from langchain_core.messages import BaseMessage, SystemMessage, HumanMessage, AIMessage
from langchain_openai import ChatOpenAI
from langgraph.graph import StateGraph, START, END
from langgraph.graph.message import add_messages
from langgraph.prebuilt import ToolNode
from langgraph.checkpoint.memory import MemorySaver
from typing import TypedDict, Annotated
from pydantic import BaseModel, Field, create_model

import config

logger = logging.getLogger("agent_core")


class AgentState(TypedDict):
    messages: Annotated[Sequence[BaseMessage], add_messages]
    sender_id: int
    sender_name: str


class McpSession:
    """Reusable MCP session using a persistent aiohttp session."""

    def __init__(self, server_url: str) -> None:
        self._server_url = server_url
        self._session_id: Optional[str] = None
        self._http = aiohttp.ClientSession()

    async def _request(self, body: dict, timeout: int = 10) -> Any:
        headers = {}
        if self._session_id:
            headers["Mcp-Session-Id"] = self._session_id

        async with self._http.post(
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

    async def _request_with_retry(self, body: dict, timeout: int = 30) -> Any:
        try:
            return await self._request(body, timeout)
        except (aiohttp.ClientResponseError, RuntimeError) as e:
            # Session expired (404) or not initialized — reinitialize and retry
            status = getattr(e, "status", 0)
            if status == 404 or "Session not found" in str(e) or "Session not initialized" in str(e):
                logger.info("MCP session expired, reinitializing...")
                self._session_id = None
                await self.initialize()
                return await self._request(body, timeout)
            raise

    async def initialize(self) -> None:
        result = await self._request_with_retry(
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

        await self._http.post(
            self._server_url,
            json={"jsonrpc": "2.0", "method": "notifications/initialized"},
            headers={"Mcp-Session-Id": self._session_id},
            timeout=aiohttp.ClientTimeout(total=5),
        )

    async def list_tools(self) -> list:
        result = await self._request_with_retry(
            {"jsonrpc": "2.0", "id": "2", "method": "tools/list"}
        )
        return (result or {}).get("tools", [])

    async def call_tool(self, name: str, arguments: dict) -> str:
        result = await self._request_with_retry(
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
        self._session: Optional[McpSession] = None
        self.name = name
        self.description = description

    async def ensure_session(self) -> None:
        if self._session is None:
            self._session = McpSession(self._server_url)
            await self._session.initialize()

    async def arun(self, **kwargs: Any) -> str:
        try:
            await self.ensure_session()
            logger.info(f"MCP tool call '{self.name}': {json.dumps(kwargs, ensure_ascii=False)}")
            return await self._session.call_tool(self.name, kwargs)
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


def _strip_think_tags(text: str) -> str:
    return re.sub(r'<think>.*?</think>', '', text, flags=re.DOTALL).strip()


def _strip_markdown(text: str) -> str:
    text = re.sub(r'\*\*(.+?)\*\*', r'\1', text)
    text = re.sub(r'__(.+?)__', r'\1', text)
    text = re.sub(r'\*(.+?)\*', r'\1', text)
    text = re.sub(r'`(.+?)`', r'\1', text)
    text = re.sub(r'^#{1,6}\s+', '', text, flags=re.MULTILINE)
    text = re.sub(r'!\[.*?\]\(.*?\)', '', text)
    text = re.sub(r'\[(.+?)\]\(.*?\)', r'\1', text)
    text = re.sub(r'^\s*[-*+]\s+', '  \u2022 ', text, flags=re.MULTILINE)
    text = re.sub(r'^\s*\d+\.\s+', '  ', text, flags=re.MULTILINE)
    text = re.sub(r'^[-*_]{3,}\s*$', '', text, flags=re.MULTILINE)
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text.strip()

def _mcp_schema_to_pydantic(name: str, schema: dict) -> type[BaseModel]:
    """Convert MCP inputSchema (JSON Schema) to a Pydantic model for StructuredTool."""
    fields = {}
    props = schema.get("properties", {})
    required = set(schema.get("required", []))

    json_to_python = {
        "string": str,
        "number": int,
        "integer": int,
        "boolean": bool,
        "array": list,
        "object": dict,
    }

    for field_name, prop in props.items():
        js_type = prop.get("type", "string")
        py_type = json_to_python.get(js_type, str)
        description = prop.get("description", "")
        if field_name in required:
            fields[field_name] = (py_type, Field(..., description=description))
        else:
            fields[field_name] = (Optional[py_type], Field(None, description=description))

    return create_model(f"{name}_args", **fields)


class ChatAgent:
    def __init__(self, mcp_url: str) -> None:
        self._mcp_url = mcp_url
        self._app: Any = None
        self._memory = MemorySaver()
        # Models to try in order; rotates on 429 (quota exceeded)
        self._model_names = [config.MODEL_NAME] + config.FALLBACK_MODELS
        self._model_idx = 0

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

                args_schema = _mcp_schema_to_pydantic(
                    td["name"], td.get("inputSchema", {})
                )

                mcp_tool = StructuredTool.from_function(
                    coroutine=make_arun_wrapper(wrapper),
                    name=td["name"],
                    description=td.get("description", "MCP Tool"),
                    args_schema=args_schema,
                )

                tools.append(mcp_tool)
                logger.info(f"  Loaded MCP tool: {td['name']}")

            logger.info(f"Loaded {len(tool_defs)} MCP tools")

        except Exception as e:
            logger.warning(f"Failed to load MCP tools: {e}")

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
                    # Rotate to next available model
                    for _ in range(len(self._model_names)):
                        self._model_idx = (self._model_idx + 1) % len(self._model_names)
                        try:
                            llm = self._create_llm()
                            llm_with_tools = llm.bind_tools(tools)
                            logger.info(f"Switched to model: {self._model_names[self._model_idx]}")
                            break
                        except NotImplementedError:
                            continue
                    # Retry with new model
                    continue
                except Exception as e:
                    logger.warning(f"LLM call failed (attempt {attempt+1}): {e}")
                    if attempt < 2:
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
                cleaned = _strip_markdown(cleaned)
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
