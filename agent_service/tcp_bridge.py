"""
Async TCP client connecting to ChatServer for real-time AI agent messaging.

Wire format (identical to Python Bridge):
  [4 bytes total_len (network byte order)] = 4 + protobuf_size
  [4 bytes msgid (network byte order)]
  [N bytes protobuf serialized data]
"""

import asyncio
import struct
import time
import logging
from typing import Optional
from proto import message_pb2 as chat

logger = logging.getLogger("tcp_bridge")

AGENT_ID = 10000
AGENT_PWD = "ai_token_123"


class ChatAgentClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 6000):
        self.host = host
        self.port = port
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self._on_message: Optional[callable] = None
        self._running = False
        self._login_ready = asyncio.Event()

    def on_message(self, callback: callable):
        """Set callback invoked on receiving ONE_CHAT_MSG."""
        self._on_message = callback

    async def connect(self):
        self._running = True
        while self._running:
            try:
                logger.info(f"Connecting to ChatServer at {self.host}:{self.port}...")
                self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
                await self._login()
                self._login_ready.set()
                logger.info("AI Agent logged in successfully, entering receive loop...")
                await self._receive_loop()
            except (ConnectionRefusedError, OSError) as e:
                logger.warning(f"Connection failed: {e}, retrying in 5s...")
                self._login_ready.clear()
                await asyncio.sleep(5)
            except Exception as e:
                logger.error(f"Unexpected error: {e}", exc_info=True)
                self._login_ready.clear()
                await asyncio.sleep(5)

    async def wait_until_ready(self):
        await self._login_ready.wait()

    async def _login(self):
        req = chat.LoginRequest()
        req.base.msgid = chat.LOGIN_MSG
        req.base.time = int(time.time() * 1000000)
        req.id = AGENT_ID
        req.password = AGENT_PWD
        await self._send_pb(req)
        # Read login ack
        len_bytes = await self.reader.readexactly(4)
        msg_len = struct.unpack("!I", len_bytes)[0]
        body = await self.reader.readexactly(msg_len)
        msgid = struct.unpack("!I", body[:4])[0]
        payload = body[4:]
        if msgid == chat.LOGIN_MSG_ACK:
            resp = chat.LoginResponse()
            resp.ParseFromString(payload)
            if resp.err_num == 0:
                logger.info("Login ACK received, agent online")
            else:
                logger.error(f"Login failed: {resp.errmsg}")
        else:
            logger.warning(f"Unexpected msgid after login: {msgid}")

    async def _send_pb(self, msg) -> None:
        data = msg.SerializeToString()
        total_len = 4 + len(data)
        msgid = msg.base.msgid
        buf = struct.pack("!II", total_len, msgid) + data
        self.writer.write(buf)
        await self.writer.drain()

    async def send_message(self, from_id: int, to_id: int, content: str) -> None:
        msg = chat.OneChatMessage()
        msg.base.msgid = chat.ONE_CHAT_MSG
        msg.base.fromid = from_id
        msg.base.toid = to_id
        msg.base.time = int(time.time() * 1000000)
        msg.message = content
        await self._send_pb(msg)

    async def _receive_loop(self):
        while self._running:
            try:
                len_bytes = await self.reader.readexactly(4)
                msg_len = struct.unpack("!I", len_bytes)[0]
                body = await self.reader.readexactly(msg_len)
                msgid = struct.unpack("!I", body[:4])[0]
                payload = body[4:]

                if msgid == chat.ONE_CHAT_MSG:
                    chat_msg = chat.OneChatMessage()
                    chat_msg.ParseFromString(payload)
                    sender_id = chat_msg.base.fromid
                    if sender_id != AGENT_ID:
                        logger.info(f"Received message from user {sender_id}: {chat_msg.message[:60]}")
                        if self._on_message:
                            asyncio.create_task(self._on_message(sender_id, chat_msg.message))

                elif msgid == chat.LOGINOUT_MSG_ACK:
                    # Re-login if kicked
                    logger.info("Session invalidated, re-logging in...")
                    self._login_ready.clear()
                    await self._login()
                    self._login_ready.set()

            except asyncio.IncompleteReadError:
                logger.warning("Connection lost, reconnecting...")
                self._login_ready.clear()
                break
            except Exception as e:
                logger.error(f"Receive error: {e}", exc_info=True)
                break

    async def close(self):
        self._running = False
        if self.writer:
            self.writer.close()
            try:
                await self.writer.wait_closed()
            except Exception:
                pass
