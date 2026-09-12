"""Unit tests for the Bridge server (frontend/bridge/main.py).

These tests run without a live ChatServer: the TCP Session class is patched,
so the FastAPI app's HTTP endpoints are exercised end-to-end at the protocol
level. Run with:  cd frontend/bridge && python -m pytest tests -q
"""

import asyncio
import base64
import sys
from pathlib import Path
from unittest.mock import AsyncMock, MagicMock, patch

import pytest

# Make sibling modules importable when running from the repo root
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from proto import message_pb2 as chat  # noqa: E402

from fastapi.testclient import TestClient  # noqa: E402


@pytest.fixture()
def app_client(monkeypatch):
    """FastAPI TestClient with the TCP layer patched out."""
    import main as bridge_main

    # _do_login must be patched where it is looked up (module attribute)
    async def fake_login(user_id: int, password: str) -> dict:
        session = MagicMock()
        session.close = AsyncMock()
        session.send = AsyncMock()
        session.user_id = user_id
        bridge_main.sessions[user_id] = session
        return {
            "err_num": 0,
            "user": {"id": user_id, "name": f"user{user_id}"},
            "friends": [{"id": 10000, "name": "AI智能助手", "state": "online"}],
            "groups": [{"id": 1, "name": "公共聊天室", "desc": "d", "members": []}],
            "offlinemsg": [],
        }

    monkeypatch.setattr(bridge_main, "_do_login", fake_login)

    async def fake_send_and_wait(session, req_msgid, data, timeout=10.0):
        return None

    monkeypatch.setattr(bridge_main, "send_and_wait", fake_send_and_wait)

    client = TestClient(bridge_main.app)
    with client:
        yield client, bridge_main

    bridge_main.sessions.clear()
    bridge_main.ws_connections.clear()
    bridge_main.pending.clear()
    bridge_main.session_passwords.clear()


def test_health(app_client):
    client, _ = app_client
    r = client.get("/api/health")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_login_and_session_state(app_client):
    client, bm = app_client
    r = client.post("/api/login", json={"id": 1, "password": "pass123"})
    assert r.status_code == 200
    data = r.json()
    assert data["err_num"] == 0
    assert data["user"]["id"] == 1
    assert any(f["id"] == 10000 for f in data["friends"])
    # session registered
    assert 1 in bm.sessions
    # health reflects online user count
    assert client.get("/api/health").json()["users_online"] == 1


def test_login_requires_fields(app_client):
    client, _ = app_client
    r = client.post("/api/login", json={"id": 1})
    assert r.status_code == 400


def test_protected_endpoints_require_login(app_client):
    client, _ = app_client
    for path, body in [
        ("/api/add_friend", {"id": 9, "friendid": 1}),
        ("/api/send_message", {"id": 9, "toid": 1, "message": "hi"}),
        ("/api/chat_history", {"id": 9, "peer_id": 1, "chat_type": 1}),
        ("/api/refresh", {"id": 9}),
    ]:
        r = client.post(path, json=body)
        assert r.status_code == 401, f"{path} should require login"


def test_logout_cleans_session(app_client):
    client, bm = app_client
    client.post("/api/login", json={"id": 2, "password": "x"})
    assert 2 in bm.sessions
    r = client.post("/api/logout", json={"id": 2})
    assert r.status_code == 200
    assert 2 not in bm.sessions


def test_refresh_requires_login(app_client):
    client, _ = app_client
    r = client.post("/api/refresh", json={"id": 42})
    assert r.status_code == 401


def test_register_validates_input(app_client):
    client, _ = app_client
    r = client.post("/api/register", json={"name": "", "password": ""})
    assert r.status_code == 400


def test_register_happy_path(app_client, monkeypatch):
    client, _ = app_client
    # Patch create_session + send_and_wait so no TCP is used
    import main as bridge_main

    resp = chat.RegisterResponse()
    resp.err_num = 0
    resp.user.id = 77
    resp.user.name = "newuser"

    fake_session = MagicMock()
    fake_session.send = AsyncMock()
    fake_session.close = AsyncMock()

    async def fake_create_session(user_id=None):
        return fake_session

    async def fake_send_and_wait(session, msgid, data, timeout=10.0):
        return resp.SerializeToString()

    monkeypatch.setattr(bridge_main, "create_session", fake_create_session)
    monkeypatch.setattr(bridge_main, "send_and_wait", fake_send_and_wait)

    r = client.post("/api/register", json={"name": "newuser", "password": "pw123456"})
    assert r.status_code == 200
    assert r.json()["user"]["id"] == 77


def test_ws_rejects_unknown_user(app_client):
    client, _ = app_client
    with client.websocket_connect("/ws/999") as ws:
        data = ws.receive_json()
        assert data["type"] == "error"


def test_decode_proto_msg_private_and_group():
    import main as bridge_main

    m = chat.OneChatMessage()
    m.base.msgid = chat.ONE_CHAT_MSG
    m.base.fromid = 5
    m.base.toid = 6
    m.base.time = 12345
    m.message = "hello"
    raw = base64.b64encode(m.SerializeToString()).decode()
    d = bridge_main._decode_proto_msg(raw)
    assert d["type"] == "chat" and d["fromid"] == 5 and d["toid"] == 6
    assert d["message"] == "hello"

    g = chat.GroupChatMessage()
    g.base.msgid = chat.GROUP_CHAT_MSG
    g.base.fromid = 5
    g.groupid = 3
    g.base.time = 999
    g.message = "group hi"
    raw = base64.b64encode(g.SerializeToString()).decode()
    d = bridge_main._decode_proto_msg(raw)
    assert d["type"] == "groupchat" and d["groupid"] == 3
