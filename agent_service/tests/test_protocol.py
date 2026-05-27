"""Tests for TCP wire protocol encoding/decoding and protobuf serialization."""

import base64
import struct
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from proto import message_pb2 as chat


def _make_frame(msgid: int, payload: bytes) -> bytes:
    body_len = 4 + len(payload)
    return struct.pack("!II", body_len, msgid) + payload


def _parse_header(data: bytes) -> tuple:
    body_len, msgid = struct.unpack("!II", data[:8])
    return body_len, msgid


def test_frame_encode_decode():
    payload = b"hello protobuf"
    msgid = 123
    frame = _make_frame(msgid, payload)
    body_len, parsed_msgid = _parse_header(frame)
    parsed_payload = frame[8:]
    assert body_len == 4 + len(payload)
    assert parsed_msgid == msgid
    assert parsed_payload == payload


def test_login_request_serialization():
    req = chat.LoginRequest()
    req.base.msgid = chat.LOGIN_MSG
    req.base.time = 1000000
    req.id = 10000
    req.password = "ai_token_123"
    raw = req.SerializeToString()
    assert len(raw) > 0
    parsed = chat.LoginRequest()
    parsed.ParseFromString(raw)
    assert parsed.base.msgid == chat.LOGIN_MSG
    assert parsed.id == 10000
    assert parsed.password == "ai_token_123"


def test_one_chat_message_roundtrip():
    msg = chat.OneChatMessage()
    msg.base.msgid = chat.ONE_CHAT_MSG
    msg.base.fromid = 10000
    msg.base.toid = 42
    msg.base.time = 2000000
    msg.message = "Hello, user!"
    raw = msg.SerializeToString()
    parsed = chat.OneChatMessage()
    parsed.ParseFromString(raw)
    assert parsed.base.fromid == 10000
    assert parsed.base.toid == 42
    assert parsed.message == "Hello, user!"


def test_base64_offline_message():
    msg = chat.OneChatMessage()
    msg.base.msgid = chat.ONE_CHAT_MSG
    msg.base.fromid = 42
    msg.base.toid = 10000
    msg.base.time = 3000000
    msg.message = "Offline hello"
    raw = msg.SerializeToString()
    b64 = base64.b64encode(raw).decode()
    # Simulate what LoginResponse.offlinemsg contains
    decoded = base64.b64decode(b64)
    parsed = chat.OneChatMessage()
    parsed.ParseFromString(decoded)
    assert parsed.base.fromid == 42
    assert parsed.message == "Offline hello"
