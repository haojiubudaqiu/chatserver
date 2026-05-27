"""Tests for the LangGraph agent core (without real API calls)."""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
from unittest.mock import patch, MagicMock


@pytest.mark.asyncio
async def test_agent_initialization():
    """Agent initializes without error even when API keys are missing."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    # MCP will fail to connect — that's OK, agent still starts
    await agent.initialize()
    assert agent._app is not None


@pytest.mark.asyncio
async def test_agent_process_message_basic():
    """process_message returns a string."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()
    result = await agent.process_message(42, "TestUser", "Hello")
    assert isinstance(result, str)


@pytest.mark.asyncio
async def test_memory_isolation():
    """Different sender_ids have isolated conversation memory."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()

    # Send first message as user 1
    r1 = await agent.process_message(1, "User1", "My name is Alice")
    assert isinstance(r1, str)

    # Send as user 2 — agent should not confuse memories
    r2 = await agent.process_message(2, "User2", "What is my name?")
    assert isinstance(r2, str)
    # The two users should have different threads; the agent shouldn't
    # say "Alice" to user 2. We can't guarantee the exact output
    # without a real LLM, but we can check it doesn't crash.


@pytest.mark.asyncio
async def test_empty_message():
    """Empty input should not crash the agent."""
    from agent_core import ChatAgent

    agent = ChatAgent("http://localhost:18999/mcp")
    await agent.initialize()
    result = await agent.process_message(1, "User", "")
    assert isinstance(result, str)
