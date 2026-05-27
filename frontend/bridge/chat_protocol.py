"""
TCP wire protocol implementation.
Maps between HTTP API and the C++ server's TCP+Protobuf protocol.

Wire format:
  [4 bytes total_len (network byte order)] = 4 + protobuf_size
  [4 bytes msgid (network byte order)]
  [N bytes protobuf serialized data]
"""

import struct
import asyncio
import time
import logging
from typing import Optional, Callable

from proto import message_pb2 as chat

logger = logging.getLogger("chat_protocol")

# Map msgid to response ack msgid
RESPONSE_MAP = {
    chat.REG_MSG: chat.REG_MSG_ACK,
    chat.LOGIN_MSG: chat.LOGIN_MSG_ACK,
    chat.ADD_FRIEND_MSG: chat.ADD_FRIEND_MSG_ACK,
    chat.CREATE_GROUP_MSG: chat.CREATE_GROUP_MSG_ACK,
    chat.ADD_GROUP_MSG: chat.ADD_GROUP_MSG_ACK,
    chat.GET_CHAT_HISTORY_MSG: chat.GET_CHAT_HISTORY_MSG_ACK,
}

# Server-pushed message types (those that arrive without a matching request)
PUSH_MESSAGE_TYPES = {
    chat.ONE_CHAT_MSG,
    chat.GROUP_CHAT_MSG,
}


def pack_message(msg) -> bytes:
    data = msg.SerializeToString()
    total_len = 4 + len(data)
    msgid = msg.base.msgid
    buf = struct.pack("!i", total_len)   # total_len
    buf += struct.pack("!i", msgid)       # msgid
    buf += data
    return buf


def make_register_request(name: str, password: str) -> bytes:
    req = chat.RegisterRequest()
    req.base.msgid = chat.REG_MSG
    req.base.time = int(time.time())
    req.name = name
    req.password = password
    return pack_message(req)


def make_login_request(userid: int, password: str) -> bytes:
    req = chat.LoginRequest()
    req.base.msgid = chat.LOGIN_MSG
    req.base.fromid = userid
    req.base.time = int(time.time())
    req.id = userid
    req.password = password
    return pack_message(req)


def make_logout_request(userid: int) -> bytes:
    req = chat.LogoutRequest()
    req.base.msgid = chat.LOGINOUT_MSG
    req.base.fromid = userid
    req.base.time = int(time.time())
    return pack_message(req)


def make_add_friend_request(userid: int, friendid: int) -> bytes:
    req = chat.AddFriendRequest()
    req.base.msgid = chat.ADD_FRIEND_MSG
    req.base.fromid = userid
    req.base.time = int(time.time())
    req.friendid = friendid
    return pack_message(req)


def make_one_chat_message(fromid: int, toid: int, text: str) -> bytes:
    req = chat.OneChatMessage()
    req.base.msgid = chat.ONE_CHAT_MSG
    req.base.fromid = fromid
    req.base.toid = toid
    req.base.time = int(time.time())
    req.message = text
    return pack_message(req)


def make_create_group_request(userid: int, name: str, desc: str) -> bytes:
    req = chat.CreateGroupRequest()
    req.base.msgid = chat.CREATE_GROUP_MSG
    req.base.fromid = userid
    req.base.time = int(time.time())
    req.groupname = name
    req.groupdesc = desc
    return pack_message(req)


def make_add_group_request(userid: int, groupid: int) -> bytes:
    req = chat.AddGroupRequest()
    req.base.msgid = chat.ADD_GROUP_MSG
    req.base.fromid = userid
    req.base.time = int(time.time())
    req.groupid = groupid
    return pack_message(req)


def make_chat_history_request(userid: int, peer_id: int, chat_type: int, limit: int = 50, before_time: int = 0) -> bytes:
    req = chat.GetChatHistoryRequest()
    req.base.msgid = chat.GET_CHAT_HISTORY_MSG
    req.base.fromid = userid
    req.base.time = int(time.time())
    req.peer_id = peer_id
    req.chat_type = chat_type
    req.limit = limit
    req.before_time = before_time
    return pack_message(req)


def make_group_chat_message(userid: int, groupid: int, text: str) -> bytes:
    req = chat.GroupChatMessage()
    req.base.msgid = chat.GROUP_CHAT_MSG
    req.base.fromid = userid
    req.base.toid = groupid
    req.base.time = int(time.time())
    req.groupid = groupid
    req.message = text
    return pack_message(req)


class Frame:
    """A parsed TCP frame."""
    def __init__(self, msgid: int, data: bytes):
        self.msgid = msgid
        self.data = data


class ParseResult:
    """Result of parsing incoming data."""
    def __init__(self):
        self.frames: list[Frame] = []
        self.remaining: bytes = b""


def parse_buffer(buf: bytes) -> ParseResult:
    result = ParseResult()
    offset = 0
    while offset + 8 <= len(buf):
        total_len = struct.unpack_from("!i", buf, offset)[0]
        if total_len <= 0 or total_len > 1024 * 1024:
            logger.error(f"Invalid total_len={total_len}, disconnecting")
            break
        if offset + 4 + total_len > len(buf):
            break
        msgid = struct.unpack_from("!i", buf, offset + 4)[0]
        payload_start = offset + 8
        payload_end = offset + 4 + total_len
        result.frames.append(Frame(msgid, buf[payload_start:payload_end]))
        offset = payload_end
    result.remaining = buf[offset:]
    return result


class Session:
    """
    Manages a single TCP connection to the C++ chat server for one user.
    Provides async send() and callback-based receive.
    """

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self._buf = b""
        self._user_id: Optional[int] = None
        self._lock = asyncio.Lock()
        self.on_message: Optional[Callable] = None  # callback(msgid, data_bytes)
        self._reader_task: Optional[asyncio.Task] = None

    @property
    def user_id(self) -> Optional[int]:
        return self._user_id

    @user_id.setter
    def user_id(self, uid: int):
        self._user_id = uid

    async def connect(self):
        self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
        self._reader_task = asyncio.create_task(self._read_loop())
        logger.info(f"TCP connected to {self.host}:{self.port}")

    async def send(self, data: bytes):
        async with self._lock:
            if self.writer:
                self.writer.write(data)
                await self.writer.drain()

    async def close(self):
        if self._reader_task:
            self._reader_task.cancel()
            self._reader_task = None
        if self.writer:
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
            self.writer = None
        logger.info(f"Session for user {self._user_id} closed")

    async def _read_loop(self):
        try:
            while True:
                chunk = await self.reader.read(4096)
                if not chunk:
                    logger.info(f"TCP connection closed for user {self._user_id}")
                    break
                self._buf += chunk
                result = parse_buffer(self._buf)
                self._buf = result.remaining
                for frame in result.frames:
                    if self.on_message:
                        ret = self.on_message(frame.msgid, frame.data)
                        if asyncio.iscoroutine(ret):
                            await ret
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"Read loop error for user {self._user_id}: {e}")
        finally:
            if self.on_message:
                try:
                    ret = self.on_message(-1, b"")
                    if asyncio.iscoroutine(ret):
                        await ret
                except Exception:
                    pass
