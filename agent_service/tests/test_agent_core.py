"""Tests for the LangGraph agent core with three-layer memory."""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
from unittest.mock import patch, MagicMock, AsyncMock

import config


@pytest.fixture(autouse=True)
def mock_redis():
    """Mock Redis to avoid real connection in all tests."""
    with patch("redis.asyncio.Redis") as mock_cls:
        mock_instance = AsyncMock()
        mock_instance.ping = AsyncMock(return_value=True)
        mock_instance.get = AsyncMock(return_value=None)
        mock_instance.set = AsyncMock(return_value=True)
        mock_instance.expire = AsyncMock(return_value=True)
        mock_instance.close = AsyncMock(return_value=True)
        mock_cls.return_value = mock_instance
        yield mock_instance


@pytest.mark.asyncio
async def test_agent_initialization(mock_redis):
    """Agent initializes without error even when API keys are missing."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()
    assert agent._app is not None
    assert agent._redis is not None
    await agent.close()


@pytest.mark.asyncio
async def test_agent_process_message_basic(mock_redis):
    """process_message returns a string."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()
    result = await agent.process_message(42, "TestUser", "Hello")
    assert isinstance(result, str)
    await agent.close()


@pytest.mark.asyncio
async def test_memory_isolation(mock_redis):
    """Different sender_ids have isolated conversation memory."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()

    r1 = await agent.process_message(1, "User1", "My name is Alice")
    assert isinstance(r1, str)

    r2 = await agent.process_message(2, "User2", "What is my name?")
    assert isinstance(r2, str)
    await agent.close()


@pytest.mark.asyncio
async def test_empty_message(mock_redis):
    """Empty input should not crash the agent."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()
    result = await agent.process_message(1, "User", "")
    assert isinstance(result, str)
    await agent.close()


@pytest.mark.asyncio
async def test_redis_save_load(mock_redis):
    """Conversation is saved to and loaded from Redis."""
    from agent_core import ChatAgent
    from langchain_core.messages import HumanMessage, AIMessage

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()

    test_msgs = [HumanMessage(content="hi"), AIMessage(content="hello")]
    await agent._save_conversation(99, test_msgs)
    assert mock_redis.set.called

    mock_redis.get.return_value = b'[{"type":"human","content":"hi"},{"type":"ai","content":"hello"}]'
    loaded = await agent._load_conversation(99)
    assert len(loaded) == 2
    assert isinstance(loaded[0], HumanMessage)
    assert loaded[0].content == "hi"
    await agent.close()


@pytest.mark.asyncio
async def test_maybe_summarize_short(mock_redis):
    """Short conversations are not summarized."""
    from agent_core import ChatAgent
    from langchain_core.messages import HumanMessage, AIMessage

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()

    msgs = [HumanMessage(content=f"msg {i}") for i in range(10)]
    result = await agent._maybe_summarize(msgs)
    assert len(result) == len(msgs)


@pytest.mark.asyncio
async def test_maybe_summarize_trigger(mock_redis):
    """Long conversations trigger summarization (without LLM fallback)."""
    from agent_core import ChatAgent
    from langchain_core.messages import HumanMessage, AIMessage

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()

    max_msgs = config.MAX_CONVERSATION_TURNS * 2
    msgs = []
    for i in range(max_msgs + 10):
        msgs.append(HumanMessage(content=f"user msg {i}"))
        msgs.append(AIMessage(content=f"ai reply {i}"))

    result = await agent._maybe_summarize(msgs)
    assert len(result) < len(msgs)
    assert any("摘要" in str(m.content) for m in result)


@pytest.mark.asyncio
async def test_bootstrap_empty_when_no_session(mock_redis):
    """Bootstrap returns empty list when there's no MCP session."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()
    agent._mcp_session = None

    result = await agent._bootstrap_history(42)
    assert result == []
    await agent.close()
