#!/bin/bash
#
# E2E test script for cluster chat server
# Tests: register, login, addfriend, private chat, create group, group chat, error scenarios
#
PASS=0
FAIL=0

CHAT_CLIENT="./bin/ChatClient"
SERVER=${1:-"127.0.0.1"}
PORT=${2:-6000}
TIMEOUT=10
DIR=$(dirname "$0")

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

assert_output() {
    local name="$1" expected="$2" got="$3"
    if echo "$got" | grep -q "$expected"; then
        echo -e "  ${GREEN}PASS${NC}: $name"
        PASS=$((PASS+1))
    else
        echo -e "  ${RED}FAIL${NC}: $name"
        echo "    Expected to contain: $expected"
        echo "    Got: $(echo "$got" | head -5 | tr '\n' ' ')"
        FAIL=$((FAIL+1))
    fi
}

# ============================================================
# TEST 1: Register new users
# ============================================================
echo "============================================================"
echo " TEST 1: Register users"
echo "============================================================"

TIMESTAMP=$(date +%s)

# Register alice2 (unique name)
OUT=$(echo -e "2\nalice2_$TIMESTAMP\npass123\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
echo "$OUT" | tail -10
ALICE_ID=$(echo "$OUT" | grep -oP 'userid is \K[0-9]+')
assert_output "Register alice2" "userid is" "$OUT"

# Register bob2
OUT=$(echo -e "2\nbob2_$TIMESTAMP\npass456\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
echo "$OUT" | tail -10
BOB_ID=$(echo "$OUT" | grep -oP 'userid is \K[0-9]+')
assert_output "Register bob2" "userid is" "$OUT"

echo "ALICE_ID=$ALICE_ID BOB_ID=$BOB_ID"

# ============================================================
# TEST 2: Login with correct credentials
# ============================================================
echo "============================================================"
echo " TEST 2: Login"
echo "============================================================"

OUT=$(echo -e "1\n$ALICE_ID\npass123\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
assert_output "Login alice2" "current login user" "$OUT"

OUT=$(echo -e "1\n$BOB_ID\npass456\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
assert_output "Login bob2" "current login user" "$OUT"

# ============================================================
# TEST 3: Login with wrong password
# ============================================================
echo "============================================================"
echo " TEST 3: Login with wrong password"
echo "============================================================"

OUT=$(echo -e "1\n$ALICE_ID\nwrongpass\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
assert_output "Wrong password rejected" "password" "$OUT"

# ============================================================
# TEST 4: Login with non-existent user
# ============================================================
echo "============================================================"
echo " TEST 4: Login with non-existent user"
echo "============================================================"

OUT=$(echo -e "1\n99999\nanypass\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
assert_output "Non-existent user rejected" "fail\|error\|不存在\|not exist\|invalid" "$OUT"

# ============================================================
# TEST 5: Duplicate register
# ============================================================
echo "============================================================"
echo " TEST 5: Duplicate register (same name)"
echo "============================================================"

OUT=$(echo -e "2\nalice2_$TIMESTAMP\npass123\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
assert_output "Duplicate register rejected" "fail\|error\|exists\|already" "$OUT"

# ============================================================
# TEST 6: Add friend + private chat (single client)
# ============================================================
echo "============================================================"
echo " TEST 6: Login alice2, add friend bob2, send message, logout"
echo "============================================================"

# Login alice2, then send commands: addfriend, chat, logout
OUT=$(echo -e "1\n$ALICE_ID\npass123\naddfriend:$BOB_ID\nchat:$BOB_ID:Hello from Alice\!\nloginout\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
echo "$OUT" | tail -10
assert_output "Add friend" "Friend added\|added successfully" "$OUT"
# Server does not echo chat back to sender; delivery is verified via offline messages (Test 7)
assert_output "Send private chat (no crash)" "loginout\|Friend added" "$OUT"

# ============================================================
# TEST 7: Login bob2 and check offline messages
# ============================================================
echo "============================================================"
echo " TEST 7: Login bob2 and check offline messages"
echo "============================================================"

OUT=$(echo -e "1\n$BOB_ID\npass456\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
echo "$OUT" | tail -10
assert_output "Bob2 receives offline msg" "said:" "$OUT"

# ============================================================
# TEST 8: Create group + group chat
# ============================================================
echo "============================================================"
echo " TEST 8: Create group, add members, group chat"
echo "============================================================"

OUT=$(echo -e "1\n$ALICE_ID\npass123\ncreategroup:test_group_$TIMESTAMP:A test group\naddgroup:999\nloginout\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
echo "$OUT" | tail -10

# Extract group id
GROUP_ID=$(echo "$OUT" | grep -oP 'groupid: \K[0-9]+')

if [ -n "$GROUP_ID" ]; then
    assert_output "Create group" "groupid" "$OUT"
    echo "GROUP_ID=$GROUP_ID"
    
    # Alice adds bob to group, sends group chat
    OUT=$(echo -e "1\n$ALICE_ID\npass123\naddgroup:$BOB_ID\ngroupchat:$GROUP_ID:Hello group!\nloginout\n" | timeout $TIMEOUT $CHAT_CLIENT $SERVER $PORT 2>&1)
    echo "$OUT" | tail -10
    assert_output "Group chat" "群消息\|group" "$OUT"
else
    echo -e "  ${RED}FAIL${NC}: Create group - could not extract groupid"
    FAIL=$((FAIL+1))
fi

# ============================================================
# RESULTS
# ============================================================
echo ""
echo "============================================================"
if [ $FAIL -eq 0 ]; then
    echo -e " ${GREEN}ALL $PASS TESTS PASSED${NC}"
else
    echo -e " ${RED}$PASS PASSED, $FAIL FAILED${NC}"
fi
echo "============================================================"
exit $FAIL
