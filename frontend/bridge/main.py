"""
Bridge server: HTTP + WebSocket ↔ TCP + Protobuf
Connects React frontend to the C++ chat server.
"""

import asyncio
import base64
import json
import logging
from contextlib import asynccontextmanager
from typing import Optional

import uvicorn
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

from chat_protocol import (
    Session, Frame,
    make_register_request, make_login_request, make_logout_request,
    make_add_friend_request, make_one_chat_message,
    make_create_group_request, make_add_group_request, make_group_chat_message,
    RESPONSE_MAP,
)
from proto import message_pb2 as chat

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
logger = logging.getLogger("bridge")

SERVER_HOST = "127.0.0.1"
SERVER_PORT = 6000

# Active sessions: user_id -> Session
sessions: dict[int, Session] = {}

# WebSocket connections: user_id -> list[WebSocket]
ws_connections: dict[int, list[WebSocket]] = {}

# Pending response futures: session_key -> { msgid -> asyncio.Future }
# session_key is user_id for persistent sessions, id(session) for temp sessions
pending: dict[int, dict[int, asyncio.Future]] = {}


@asynccontextmanager
async def lifespan(app: FastAPI):
    logger.info("Bridge server starting")
    yield
    logger.info("Bridge server shutting down")
    for uid, session in list(sessions.items()):
        await session.close()
    sessions.clear()
    ws_connections.clear()


app = FastAPI(title="Chat Bridge", version="1.0.0", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


def _make_msg_callback(session_key: int, is_persistent: bool = False):
    """Create callback for a session identified by session_key."""
    async def cb(msgid: int, data: bytes):
        if msgid == -1:
            logger.info(f"Session {session_key} disconnected")
            if is_persistent and isinstance(session_key, int):
                await _cleanup_session(session_key)
            return
        logger.debug(f"Callback: session={session_key} msgid={msgid} data_len={len(data)}")
        if session_key in pending:
            fut = pending[session_key].pop(msgid, None)
            if fut and not fut.done():
                logger.info(f"Resolved pending future for session={session_key} msgid={msgid}")
                fut.set_result(data)
                return
        # Not a response to a request - dispatch to WS push
        if isinstance(session_key, int) and session_key in sessions:
            logger.info(f"Dispatching push: session={session_key} msgid={msgid}")
            await _dispatch_push(session_key, msgid, data)
        else:
            logger.warning(f"No handler for session={session_key} msgid={msgid}")
    return cb


async def _dispatch_push(user_id: int, msgid: int, data: bytes):
    """Dispatch a push message (chat/groupchat) to the user's WebSocket connections."""
    payload = None
    if msgid == chat.ONE_CHAT_MSG:
        msg = chat.OneChatMessage()
        msg.ParseFromString(data)
        payload = json.dumps({
            "type": "chat", "fromid": msg.base.fromid, "toid": msg.base.toid,
            "time": msg.base.time, "message": msg.message,
        })
        logger.info(f"Push ONE_CHAT_MSG to user={user_id} from={msg.base.fromid}: {msg.message[:50]}")
    elif msgid == chat.GROUP_CHAT_MSG:
        msg = chat.GroupChatMessage()
        msg.ParseFromString(data)
        payload = json.dumps({
            "type": "groupchat", "fromid": msg.base.fromid,
            "groupid": msg.groupid, "time": msg.base.time, "message": msg.message,
        })
        logger.info(f"Push GROUP_CHAT_MSG to user={user_id} from={msg.base.fromid}")

    if payload and user_id in ws_connections:
        logger.info(f"WS connections for user={user_id}: {len(ws_connections[user_id])}")
        dead = []
        for ws in ws_connections[user_id]:
            try:
                await ws.send_text(payload)
                logger.info(f"WS sent to user={user_id}")
            except Exception as e:
                logger.error(f"WS send error for user={user_id}: {e}")
                dead.append(ws)
        for ws in dead:
            try:
                ws_connections[user_id].remove(ws)
            except ValueError:
                pass
    else:
        logger.warning(f"No WS connections for user={user_id}, payload={payload is not None}")


async def create_session(user_id: Optional[int] = None) -> Session:
    """Create and connect a new TCP session."""
    session = Session(SERVER_HOST, SERVER_PORT)
    if user_id is not None:
        session.user_id = user_id
    skey = user_id if user_id is not None else id(session)
    session.on_message = _make_msg_callback(skey)
    await session.connect()
    return session


async def send_and_wait(session: Session, req_msgid: int, data: bytes, timeout: float = 10.0) -> Optional[bytes]:
    """Send a request and wait for the matching ack response."""
    ack_msgid = RESPONSE_MAP.get(req_msgid)
    if ack_msgid is None:
        await session.send(data)
        return None

    skey = session.user_id if session.user_id is not None else id(session)
    if skey not in pending:
        pending[skey] = {}
    fut = asyncio.get_event_loop().create_future()
    pending[skey][ack_msgid] = fut

    await session.send(data)
    try:
        return await asyncio.wait_for(fut, timeout=timeout)
    except asyncio.TimeoutError:
        pending[skey].pop(ack_msgid, None)
        raise TimeoutError("Server response timeout")


async def _cleanup_session(user_id: int):
    session = sessions.pop(user_id, None)
    if session:
        await session.close()
    wss = ws_connections.pop(user_id, None)
    if wss:
        for ws in wss:
            try:
                await ws.close()
            except Exception:
                pass
    pending.pop(user_id, None)


# ─── REST API ────────────────────────────────────────────────

@app.post("/api/register")
async def api_register(body: dict):
    name = body.get("name", "").strip()
    password = body.get("password", "").strip()
    if not name or not password:
        raise HTTPException(400, "name and password required")

    session = await create_session()
    try:
        resp_data = await send_and_wait(session, chat.REG_MSG, make_register_request(name, password))
        if resp_data is None:
            raise HTTPException(502, "No response from server")
        resp = chat.RegisterResponse()
        resp.ParseFromString(resp_data)
        if resp.err_num != 0:
            raise HTTPException(400, resp.errmsg or "Registration failed")
        return {"err_num": 0, "user": {"id": resp.user.id, "name": resp.user.name}}
    finally:
        await session.close()


@app.post("/api/login")
async def api_login(body: dict):
    user_id = body.get("id")
    password = body.get("password", "").strip()
    if not user_id or not password:
        raise HTTPException(400, "id and password required")

    # If user already has a session, reuse it
    if user_id in sessions:
        try:
            await send_and_wait(sessions[user_id], chat.LOGINOUT_MSG, make_logout_request(user_id), timeout=3)
        except Exception:
            pass
        await _cleanup_session(user_id)

    session = await create_session(user_id)
    try:
        resp_data = await send_and_wait(session, chat.LOGIN_MSG, make_login_request(user_id, password))
    except Exception as e:
        await session.close()
        raise HTTPException(502, str(e))

    if resp_data is None:
        await session.close()
        raise HTTPException(502, "No response from server")

    resp = chat.LoginResponse()
    resp.ParseFromString(resp_data)
    if resp.err_num != 0:
        await session.close()
        raise HTTPException(403, resp.errmsg or "Login failed")

    # Transfer to persistent session
    sessions[user_id] = session
    session.user_id = user_id
    session.on_message = _make_msg_callback(user_id, is_persistent=True)

    offlines = []
    for raw_b64 in resp.offlinemsg:
        raw = base64.b64decode(raw_b64)
        # Detect message type from protobuf's own msgid field
        probe = chat.OneChatMessage()
        probe.ParseFromString(raw)
        msgtype = probe.base.msgid
        if msgtype == chat.GROUP_CHAT_MSG:
            inner = chat.GroupChatMessage()
            inner.ParseFromString(raw)
            offlines.append({
                "type": "groupchat", "fromid": inner.base.fromid,
                "groupid": inner.groupid, "time": inner.base.time,
                "message": inner.message,
            })
        else:
            # Default to OneChatMessage
            offlines.append({
                "type": "chat", "fromid": probe.base.fromid,
                "toid": probe.base.toid, "time": probe.base.time,
                "message": probe.message,
            })

    friends = [{"id": f.id, "name": f.name, "state": f.state} for f in resp.friends]

    groups = [
        {
            "id": g.id, "name": g.groupname, "desc": g.groupdesc,
            "members": [{"id": u.id, "name": u.name, "state": u.state, "role": u.role} for u in g.users],
        }
        for g in resp.groups
    ]

    return {
        "err_num": 0, "user": {"id": resp.user.id, "name": resp.user.name},
        "friends": friends, "groups": groups, "offlinemsg": offlines,
    }


@app.post("/api/logout")
async def api_logout(body: dict):
    user_id = body.get("id")
    if not user_id:
        raise HTTPException(400, "id required")
    session = sessions.get(user_id)
    if not session:
        return {"err_num": 0, "message": "already logged out"}
    try:
        await send_and_wait(session, chat.LOGINOUT_MSG, make_logout_request(user_id), timeout=3)
    except Exception:
        pass
    await _cleanup_session(user_id)
    return {"err_num": 0}


@app.post("/api/add_friend")
async def api_add_friend(body: dict):
    user_id = body.get("id")
    friend_id = body.get("friendid")
    if not user_id or not friend_id:
        raise HTTPException(400, "id and friendid required")
    session = sessions.get(user_id)
    if not session:
        raise HTTPException(401, "Not logged in")
    resp_data = await send_and_wait(session, chat.ADD_FRIEND_MSG, make_add_friend_request(user_id, friend_id))
    if resp_data is None:
        raise HTTPException(502, "No response")
    resp = chat.AddFriendResponse()
    resp.ParseFromString(resp_data)
    if resp.err_num != 0:
        raise HTTPException(400, resp.errmsg or "Failed to add friend")
    return {"err_num": 0}


@app.post("/api/send_message")
async def api_send_message(body: dict):
    user_id = body.get("id")
    to_id = body.get("toid")
    text = body.get("message", "").strip()
    if not user_id or not to_id or not text:
        raise HTTPException(400, "id, toid, message required")
    session = sessions.get(user_id)
    if not session:
        raise HTTPException(401, "Not logged in")
    logger.info(f"SEND_MSG user={user_id} -> to={to_id}: {text[:50]}")
    await session.send(make_one_chat_message(user_id, to_id, text))
    logger.info(f"SEND_MSG sent OK user={user_id} -> to={to_id}")
    return {"err_num": 0}


@app.post("/api/create_group")
async def api_create_group(body: dict):
    user_id = body.get("id")
    name = body.get("name", "").strip()
    desc = body.get("desc", "").strip()
    if not user_id or not name:
        raise HTTPException(400, "id and name required")
    session = sessions.get(user_id)
    if not session:
        raise HTTPException(401, "Not logged in")
    resp_data = await send_and_wait(session, chat.CREATE_GROUP_MSG, make_create_group_request(user_id, name, desc))
    if resp_data is None:
        raise HTTPException(502, "No response")
    resp = chat.CreateGroupResponse()
    resp.ParseFromString(resp_data)
    if resp.err_num != 0:
        raise HTTPException(400, resp.errmsg or "Failed to create group")
    return {"err_num": 0, "groupid": resp.groupid}


@app.post("/api/join_group")
async def api_join_group(body: dict):
    user_id = body.get("id")
    group_id = body.get("groupid")
    if not user_id or not group_id:
        raise HTTPException(400, "id and groupid required")
    session = sessions.get(user_id)
    if not session:
        raise HTTPException(401, "Not logged in")
    resp_data = await send_and_wait(session, chat.ADD_GROUP_MSG, make_add_group_request(user_id, group_id))
    if resp_data is None:
        raise HTTPException(502, "No response")
    resp = chat.AddGroupResponse()
    resp.ParseFromString(resp_data)
    if resp.err_num != 0:
        raise HTTPException(400, resp.errmsg or "Failed to join group")
    return {"err_num": 0}


@app.post("/api/send_group_message")
async def api_send_group_message(body: dict):
    user_id = body.get("id")
    group_id = body.get("groupid")
    text = body.get("message", "").strip()
    if not user_id or not group_id or not text:
        raise HTTPException(400, "id, groupid, message required")
    session = sessions.get(user_id)
    if not session:
        raise HTTPException(401, "Not logged in")
    await session.send(make_group_chat_message(user_id, group_id, text))
    return {"err_num": 0}


@app.get("/api/me/{user_id}")
async def api_get_me(user_id: int):
    """Get current user info (friends + groups)."""
    session = sessions.get(user_id)
    if not session:
        raise HTTPException(401, "Not logged in")
    user = chat.User()
    # We can't query via TCP, so we read from DB via the model
    # For now, return cached data from login
    # This needs a server-side refresh endpoint
    raise HTTPException(501, "Use /api/login response data; refresh not yet supported")


# ─── WebSocket ───────────────────────────────────────────────

@app.websocket("/ws/{user_id}")
async def websocket_endpoint(ws: WebSocket, user_id: int):
    await ws.accept()
    if user_id not in sessions:
        await ws.send_text(json.dumps({"type": "error", "message": "Not logged in"}))
        await ws.close()
        return

    if user_id not in ws_connections:
        ws_connections[user_id] = []
    ws_connections[user_id].append(ws)
    logger.info(f"WS connected for user {user_id}")

    try:
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        try:
            ws_connections[user_id].remove(ws)
        except (ValueError, KeyError):
            pass
        logger.info(f"WS disconnected for user {user_id}")


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=False)
