#!/bin/bash
# Cross-server messaging test using concurrent clients
set -e

CHAT_CLIENT="./bin/ChatClient"
SERVER1="127.0.0.1"
PORT1=6000
SERVER2="127.0.0.1"
PORT2=6001
TIMEOUT=15

# Register test users
TIMESTAMP=$(date +%s)
echo "=== Register users ==="
OUT1=$(echo -e "2\ncross_a2_$TIMESTAMP\npass123\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER1 $PORT1 2>&1)
A_ID=$(echo "$OUT1" | grep -oP 'userid is \K[0-9]+')
echo "User A: $A_ID (on port $PORT1)"

OUT2=$(echo -e "2\ncross_b2_$TIMESTAMP\npass456\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER2 $PORT2 2>&1)
B_ID=$(echo "$OUT2" | grep -oP 'userid is \K[0-9]+')
echo "User B: $B_ID (on port $PORT2)"

# First, add friend relationship (both on same server to make it simple)
echo "=== Add friend (A adds B) ==="
OUT3=$(echo -e "1\n$A_ID\npass123\naddfriend:$B_ID\nloginout\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER1 $PORT1 2>&1)
echo "$OUT3" | grep -q "Friend added successfully" && echo "Friend added: OK" || echo "Friend added: FAIL"

# Concurrent test: B stays logged in on server 2, A sends message from server 1
echo "=== Concurrent cross-server test ==="

# Create temp file for B's output
B_OUTPUT=$(mktemp)

# Start B in background, feeding it login + keeping stdin alive for 15 seconds
{ echo -e "1\n$B_ID\npass456\n"; sleep 15; } | timeout $TIMEOUT $CHAT_CLIENT $SERVER2 $PORT2 > "$B_OUTPUT" 2>&1 &
B_PID=$!
sleep 3  # Wait for B to log in

# Now A logs in on server 1 and sends chat
echo -e "1\n$A_ID\npass123\nchat:$B_ID:Hello from server 1!\nloginout\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER1 $PORT1 > /dev/null 2>&1
sleep 2  # Wait for message delivery

# Stop B's client
kill $B_PID 2>/dev/null || true
wait $B_PID 2>/dev/null || true

# Check B's output for the message
echo ""
echo "=== B's output ==="
cat "$B_OUTPUT" | head -30
echo ""

if grep -q "said:" "$B_OUTPUT"; then
    echo -e "\n\033[0;32mPASS: Cross-server message received by B\033[0m"
    B_MSG=$(grep "said:" "$B_OUTPUT" | head -1)
    echo "  $B_MSG"
    RESULT=0
else
    echo -e "\n\033[0;31mFAIL: Cross-server message NOT received\033[0m"
    # Check offline messages as fallback
    docker compose exec mysql-master mysql -uroot -p'Sf523416&111' chat -e "SELECT id, userid, length(message) FROM offlinemessage WHERE userid=$B_ID;" 2>&1 | grep -v Warning
    RESULT=1
fi

rm -f "$B_OUTPUT"
exit $RESULT
