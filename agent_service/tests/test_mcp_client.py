"""Tests for MCP client session management using a mock HTTP server."""

import json
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import pytest
import pytest_asyncio
from aiohttp import web


# ── Mock MCP Server ──────────────────────────────────────────

def _make_mcp_app(tools: list = None, fail_initialize: bool = False):
    """Create a mock MCP HTTP server that mimics Streamable HTTP."""

    sessions = {}
    initialized = set()

    async def handle_post(request):
        body = await request.json()
        method = body.get("method")
        req_id = body.get("id")
        sid = request.headers.get("Mcp-Session-Id", "")

        if method == "initialize":
            new_sid = "mock-session-001"
            sessions[new_sid] = True
            resp = web.json_response(
                {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "protocolVersion": "2025-03-26",
                        "serverInfo": {"name": "MockServer", "version": "1.0"},
                    },
                }
            )
            resp.headers["Mcp-Session-Id"] = new_sid
            return resp

        elif method == "notifications/initialized":
            initialized.add(sid)
            return web.json_response({"jsonrpc": "2.0"})

        elif method == "tools/list":
            if sid not in initialized:
                return web.json_response(
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "error": {"code": -32600, "message": "Session not initialized"},
                    },
                    status=400,
                )
            return web.json_response(
                {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "tools": tools or [
                            {
                                "name": "echo",
                                "description": "Echo the input",
                                "inputSchema": {
                                    "type": "object",
                                    "properties": {
                                        "text": {"type": "string"}
                                    },
                                },
                            }
                        ]
                    },
                }
            )

        elif method == "tools/call":
            if sid not in initialized:
                return web.json_response(
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "error": {"code": -32600, "message": "Session not initialized"},
                    },
                    status=400,
                )
            params = body.get("params", {})
            name = params.get("name", "")
            args = params.get("arguments", {})
            return web.json_response(
                {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "content": [
                            {"type": "text", "text": f"{name} called with {json.dumps(args)}"}
                        ]
                    },
                }
            )

        return web.json_response(
            {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32601, "message": "Method not found"}}
        )

    app = web.Application()
    app.router.add_post("/mcp", handle_post)
    return app


@pytest_asyncio.fixture
async def mcp_server(unused_tcp_port):
    """Start a mock MCP server on a random port."""
    app = _make_mcp_app()
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", unused_tcp_port)
    await site.start()
    yield f"http://127.0.0.1:{unused_tcp_port}/mcp"
    await runner.cleanup()


@pytest.mark.asyncio
async def test_mcp_initialize(mcp_server):
    from agent_core import McpSession

    session = McpSession(mcp_server)
    await session.initialize()
    assert session._session_id is not None


@pytest.mark.asyncio
async def test_mcp_tools_list(mcp_server):
    from agent_core import McpSession

    session = McpSession(mcp_server)
    await session.initialize()
    tools = await session.list_tools()
    assert isinstance(tools, list)
    assert any(t["name"] == "echo" for t in tools)


@pytest.mark.asyncio
async def test_mcp_call_tool(mcp_server):
    from agent_core import McpSession

    session = McpSession(mcp_server)
    await session.initialize()
    result = await session.call_tool("echo", {"text": "hello"})
    assert "hello" in result
