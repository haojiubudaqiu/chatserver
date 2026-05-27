import os


# ── AI Agent Identity ──────────────────────────────────────────
AI_USER_ID = int(os.environ.get("AI_USER_ID", "10000"))
AI_PASSWORD = os.environ.get("AI_PASSWORD", "ai_token_123")

# ── ChatServer TCP Connection ───────────────────────────────────
CHAT_SERVER_HOST = os.environ.get("CHAT_SERVER_HOST", "127.0.0.1")
CHAT_SERVER_PORT = int(os.environ.get("CHAT_SERVER_PORT", "6000"))

# ── MCP (Model Context Protocol) ────────────────────────────────
MCP_SERVER_URL = os.environ.get("MCP_SERVER_URL", "http://127.0.0.1:8888/mcp")

# ── ModelScope (LLM) ───────────────────────────────────────────
MODELSCOPE_API_KEY = os.environ.get("MODELSCOPE_API_KEY", "")
MODELSCOPE_BASE_URL = os.environ.get("MODELSCOPE_BASE_URL", "https://api-inference.modelscope.cn/v1")
MODEL_NAME = os.environ.get("MODEL_NAME", "qwen-max")

# ── Tavily (Web Search) ────────────────────────────────────────
TAVILY_API_KEY = os.environ.get("TAVILY_API_KEY", "")

# ── Runtime ─────────────────────────────────────────────────────
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO")
RECONNECT_DELAY = int(os.environ.get("RECONNECT_DELAY", "5"))


def has_model_api_key() -> bool:
    return bool(MODELSCOPE_API_KEY)


def has_tavily_key() -> bool:
    return bool(TAVILY_API_KEY)
