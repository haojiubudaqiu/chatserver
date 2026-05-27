"""Tests for the TCP bridge client."""

import struct
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
from unittest.mock import AsyncMock, MagicMock

from proto import message_pb2 as chat


@pytest.mark.asyncio
async def test_send_proto_format():
    """Verify the framing format sent over the wire is correct."""
    from tcp_bridge import AgentTcpClient

    writer = MagicMock()
    writer.write = MagicMock()
    writer.drain = AsyncMock()

    client = AgentTcpClient("127.0.0.1", 6000, 10000, "pwd")
    client._writer = writer

    await client.send_chat_message(42, "Hello")

    # Check that write was called with bytes
    assert writer.write.called
    call_bytes = writer.write.call_args[0][0]

    # Parse the frame
    body_len, msgid = struct.unpack("!II", call_bytes[:8])
    payload = call_bytes[8:]

    assert body_len == 4 + len(payload)
    assert msgid == chat.ONE_CHAT_MSG

    # Decode the protobuf
    msg = chat.OneChatMessage()
    msg.ParseFromString(payload)
    assert msg.base.fromid == 10000
    assert msg.base.toid == 42
    assert msg.message == "Hello"


@pytest.mark.asyncio
async def test_self_message_filter():
    """Messages from ourselves (fromid == agent_id) must be filtered out."""
    from tcp_bridge import AgentTcpClient

    callback = MagicMock()
    client = AgentTcpClient(
        "127.0.0.1", 6000, 10000, "pwd", on_message=callback
    )

    # Simulate receiving a message FROM the agent itself
    msg = chat.OneChatMessage()
    msg.base.msgid = chat.ONE_CHAT_MSG
    msg.base.fromid = 10000
    msg.base.toid = 42
    msg.base.time = 12345
    msg.message = "I said this"
    raw = msg.SerializeToString()

    # Manually trigger the receive path
    client._reader = AsyncMock()
    header = struct.pack("!II", 4 + len(raw), chat.ONE_CHAT_MSG)
    client._reader.readexactly = AsyncMock(side_effect=[header, raw])

    # Run one cycle of receive loop
    await client._receive_loop()

    # Callback must NOT be called for self-messages
    callback.assert_not_called()
