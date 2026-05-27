"""
Async TCP client connecting to ChatServer for real-time AI agent messaging.

Wire format (identical to Python Bridge + C++):
  [4 bytes body_len (network byte order, = 4 + protobuf_size)]
  [4 bytes msgid (network byte order)]
  [N bytes protobuf serialized data]
"""

import asyncio
import base64
import struct
import time
import logging
from typing import Callable, Optional

from proto import message_pb2 as chat

logger = logging.getLogger("tcp_bridge")


class AgentTcpClient:
    """Async TCP client that connects to ChatServer as the AI agent user."""

    def __init__(
        self,
        host: str,
        port: int,
        agent_id: int,
        password: str,
        on_message: Optional[Callable[[int, str, int], None]] = None,
    ):
        self._host = host
        self._port = port
        self._agent_id = agent_id
        self._password = password
        self._on_message = on_message
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._running = False

    async def run(self) -> None:
        """Connect, login, and enter receive loop. Reconnects on failure."""
        self._running = True
        while self._running:
            try:
                logger.info(f"Connecting to {self._host}:{self._port} ...")
                self._reader, self._writer = await asyncio.open_connection(
                    self._host, self._port
                )
                await self._login()
                logger.info("Login successful, entering receive loop")
                await self._receive_loop()
            except asyncio.CancelledError:
                break
            except (ConnectionRefusedError, OSError) as e:
                logger.warning(f"Connection failed: {e}, retrying in 5s...")
                await asyncio.sleep(5)
            except Exception as e:
                logger.error(f"Unexpected error in run loop: {e}", exc_info=True)
                await asyncio.sleep(5)

    async def stop(self) -> None:
        self._running = False
        if self._writer:
            self._writer.close()
            try:
                await self._writer.wait_closed()
            except Exception:
                pass

    async def send_chat_message(self, to_id: int, content: str) -> None:
        """Send a private OneChatMessage to a user."""
        msg = chat.OneChatMessage()
        msg.base.msgid = chat.ONE_CHAT_MSG
        msg.base.fromid = self._agent_id
        msg.base.toid = to_id
        msg.base.time = int(time.time() * 1000000)
        msg.message = content
        await self._send_raw(msg)

    # ── Internal ────────────────────────────────────────────────

    async def _login(self) -> None:
        """Send LoginRequest, read LoginResponse, dispatch offline messages."""
        req = chat.LoginRequest()
        req.base.msgid = chat.LOGIN_MSG
        req.base.time = int(time.time() * 1000000)
        req.id = self._agent_id
        req.password = self._password
        await self._send_raw(req)

        # Read response header: 8 bytes (body_len + msgid)
        header = await self._reader.readexactly(8)
        body_len, msgid = struct.unpack("!II", header)
        payload = await self._reader.readexactly(body_len - 4)

        if msgid != chat.LOGIN_MSG_ACK:
            raise RuntimeError(f"Expected LOGIN_MSG_ACK, got msgid={msgid}")

        resp = chat.LoginResponse()
        resp.ParseFromString(payload)
        if resp.err_num != 0:
            raise RuntimeError(f"Login failed: {resp.errmsg}")

        # Dispatch offline messages (base64-encoded protobuf)
        for raw_b64 in resp.offlinemsg:
            try:
                raw = base64.b64decode(raw_b64)
                probe = chat.OneChatMessage()
                probe.ParseFromString(raw)
                if probe.base.msgid == chat.GROUP_CHAT_MSG:
                    inner = chat.GroupChatMessage()
                    inner.ParseFromString(raw)
                    sender_id = inner.base.fromid
                    content = inner.message
                    timestamp = inner.base.time
                else:
                    sender_id = probe.base.fromid
                    content = probe.message
                    timestamp = probe.base.time
                if sender_id != self._agent_id and self._on_message:
                    if asyncio.iscoroutinefunction(self._on_message):
                        asyncio.create_task(self._on_message(sender_id, content, timestamp))
                    else:
                        self._on_message(sender_id, content, timestamp)
            except Exception as e:
                logger.warning(f"Failed to dispatch offline message: {e}")

    async def _receive_loop(self) -> None:
        """Continuously read messages from ChatServer."""
        while self._running:
            try:
                header = await self._reader.readexactly(8)
                body_len, msgid = struct.unpack("!II", header)
                payload = await self._reader.readexactly(body_len - 4)

                if msgid == chat.ONE_CHAT_MSG:
                    chat_msg = chat.OneChatMessage()
                    chat_msg.ParseFromString(payload)

                    # 防自循环：不处理自己发出的消息
                    if chat_msg.base.fromid == self._agent_id:
                        continue

                    if self._on_message:
                        if asyncio.iscoroutinefunction(self._on_message):
                            asyncio.create_task(
                                self._on_message(
                                    chat_msg.base.fromid,
                                    chat_msg.message,
                                    chat_msg.base.time,
                                )
                            )
                        else:
                            self._on_message(
                                chat_msg.base.fromid,
                                chat_msg.message,
                                chat_msg.base.time,
                            )

            except asyncio.IncompleteReadError:
                logger.warning("Connection lost, reconnecting...")
                break
            except Exception as e:
                logger.error(f"Receive error: {e}", exc_info=True)
                break

    async def _send_raw(self, msg) -> None:
        """Serialize protobuf message and send with correct framing."""
        data = msg.SerializeToString()
        body_len = 4 + len(data)
        msgid = msg.base.msgid
        frame = struct.pack("!II", body_len, msgid) + data
        self._writer.write(frame)
        await self._writer.drain()
