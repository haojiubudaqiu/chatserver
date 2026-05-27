"""
AI Agent service entry point.

Connects to ChatServer via TCP for real-time messaging,
and uses LangGraph for AI reasoning with MCP tool access.
"""

import asyncio
import logging
import os
import sys

from tcp_bridge import ChatAgentClient, AGENT_ID
from agent_core import AgentCore

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("main")

CHAT_HOST = os.environ.get("CHAT_SERVER_HOST", "127.0.0.1")
CHAT_PORT = int(os.environ.get("CHAT_SERVER_PORT", "6000"))


async def main():
    logger.info("Starting AI Agent service...")
    logger.info(f"ChatServer TCP: {CHAT_HOST}:{CHAT_PORT}")
    logger.info(f"MCP Server URL: {os.environ.get('MCP_SERVER_URL', 'http://localhost:8888/mcp')}")

    # Initialize LangGraph agent
    agent = AgentCore()
    try:
        await agent.initialize()
    except Exception as e:
        logger.error(f"Failed to initialize agent: {e}", exc_info=True)
        sys.exit(1)

    # Initialize TCP client
    client = ChatAgentClient(host=CHAT_HOST, port=CHAT_PORT)

    # Set message handler
    async def on_user_message(sender_id: int, text: str):
        try:
            sender_name = f"User#{sender_id}"
            logger.info(f"Processing message from {sender_name}: {text[:80]}")
            reply = await agent.process_message(sender_id, sender_name, text)
            if reply:
                await client.send_message(AGENT_ID, sender_id, reply)
                logger.info(f"Replied to {sender_name}: {reply[:80]}")
        except Exception as e:
            logger.error(f"Error processing message: {e}", exc_info=True)

    client.on_message(on_user_message)

    # Connect and run
    try:
        await client.connect()
    except KeyboardInterrupt:
        logger.info("Shutting down...")
    finally:
        await client.close()
        await agent.cleanup()
        logger.info("AI Agent service stopped")


if __name__ == "__main__":
    asyncio.run(main())
