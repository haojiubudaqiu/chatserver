"""
AI Agent service entry point.

Connects to ChatServer via TCP for real-time messaging,
and uses LangGraph for AI reasoning with MCP tool access.
"""

import asyncio
import logging
import sys

import config
from agent_core import ChatAgent
from tcp_bridge import AgentTcpClient

logging.basicConfig(
    level=getattr(logging, config.LOG_LEVEL, logging.INFO),
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("main")


def print_banner() -> None:
    print("=" * 56)
    print("  AI Agent Service — ChatServer Intelligent Assistant")
    print("=" * 56)
    print(f"  ChatServer TCP : {config.CHAT_SERVER_HOST}:{config.CHAT_SERVER_PORT}")
    print(f"  MCP Server URL : {config.MCP_SERVER_URL}")
    print(f"  AI User ID     : {config.AI_USER_ID}")
    print(f"  LLM Model      : {config.MODEL_NAME}")
    print(f"  Web Search     : {'enabled' if config.has_tavily_key() else 'disabled'}")
    print(f"  LLM API Key    : {'set' if config.has_model_api_key() else 'NOT SET'}")
    print("=" * 56)


async def main() -> None:
    print_banner()

    # 1. Initialize LangGraph agent
    agent = ChatAgent(config.MCP_SERVER_URL)
    try:
        await agent.initialize()
    except Exception as e:
        logger.error(f"Agent initialization failed: {e}", exc_info=True)
        sys.exit(1)

    # 2. Message handler
    async def handle_user_message(sender_id: int, content: str, timestamp: int) -> None:
        try:
            sender_name = f"User#{sender_id}"
            logger.info(f"Processing message from {sender_name}: {content[:80]}")
            reply = await agent.process_message(sender_id, sender_name, content)
            if reply and reply.strip():
                await tcp_client.send_chat_message(sender_id, reply)
                logger.info(f"Replied to {sender_name}: {reply[:80]}")
        except Exception as e:
            logger.error(f"Error handling message from user {sender_id}: {e}", exc_info=True)

    # 3. TCP client (runs its own reconnect loop)
    tcp_client = AgentTcpClient(
        host=config.CHAT_SERVER_HOST,
        port=config.CHAT_SERVER_PORT,
        agent_id=config.AI_USER_ID,
        password=config.AI_PASSWORD,
        on_message=handle_user_message,
    )

    try:
        await tcp_client.run()
    except asyncio.CancelledError:
        pass
    finally:
        await tcp_client.stop()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Shutting down gracefully...")
