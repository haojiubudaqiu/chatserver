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

# Primary model (first tried). Can be overridden via MODEL_NAME env var.
MODEL_NAME = os.environ.get("MODEL_NAME", "MiniMax/MiniMax-M1-80k")

# Fallback models tried in order when primary model hits rate limits.
# See docs/AGENT_USER_GUIDE.md for recommended models:
#   - MiniMax/MiniMax-M1-80k: fastest, good tool support (our new default)
#   - ZhipuAI/GLM-5: balanced, good tool support (was default, often quota-full)
#   - deepseek-ai/DeepSeek-R1-0528: strong reasoning, slower
FALLBACK_MODELS = [
    "ZhipuAI/GLM-5",
    "deepseek-ai/DeepSeek-R1-0528",
]

# ── Tavily (Web Search) ────────────────────────────────────────
TAVILY_API_KEY = os.environ.get("TAVILY_API_KEY", "")

# ── Runtime ─────────────────────────────────────────────────────
LOG_LEVEL = os.environ.get("LOG_LEVEL", "INFO")
RECONNECT_DELAY = int(os.environ.get("RECONNECT_DELAY", "5"))


def has_model_api_key() -> bool:
    return bool(MODELSCOPE_API_KEY)


def has_tavily_key() -> bool:
    return bool(TAVILY_API_KEY)
