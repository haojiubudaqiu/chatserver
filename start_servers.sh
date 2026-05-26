#!/bin/bash
# Start 3 local ChatServer instances for development
# Requires: docker compose up -d (MySQL, Redis, Kafka, Nginx)

set -e

KILL_ONLY=false
if [ "$1" = "--kill" ]; then
    KILL_ONLY=true
fi

echo "Stopping existing ChatServer instances..."
killall -9 ChatServer 2>/dev/null || true
sleep 1

if [ "$KILL_ONLY" = true ]; then
    echo "All ChatServer instances stopped."
    exit 0
fi

export KAFKA_HOST=localhost
export KAFKA_PORT=9093

echo "Starting ChatServer on ports 6000, 6001, 6002..."

SERVER_PORT=6000 nohup ./bin/ChatServer 0.0.0.0 6000 > /tmp/server0.log 2>&1 &
sleep 3
echo "  PID $! - Port 6000 (log: /tmp/server0.log)"

SERVER_PORT=6001 nohup ./bin/ChatServer 0.0.0.0 6001 > /tmp/server1.log 2>&1 &
sleep 3
echo "  PID $! - Port 6001 (log: /tmp/server1.log)"

SERVER_PORT=6002 nohup ./bin/ChatServer 0.0.0.0 6002 > /tmp/server2.log 2>&1 &
sleep 3
echo "  PID $! - Port 6002 (log: /tmp/server2.log)"

echo ""
echo "All servers started. Check:"
ss -tlnp | grep 600[0-2]
echo ""
echo "View logs: tail -f /tmp/server{0,1,2}.log"
