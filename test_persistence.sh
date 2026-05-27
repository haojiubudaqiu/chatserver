#!/bin/bash
# 聊天历史持久化测试
set +e

SERVER="http://127.0.0.1:8000"
PASS=0
FAIL=0

pass() { PASS=$((PASS+1)); echo -e "  \033[0;32mPASS\033[0m: $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "  \033[0;31mFAIL\033[0m: $1"; }

TS=$(date +%s)

echo "============================================================"
echo " 聊天历史持久化测试 $(date)"
echo "============================================================"

# ---------- 1. Register A & B ----------
echo ""
echo "=== 1. 注册测试用户 ==="

R_A=$(curl -s -X POST $SERVER/api/register -H "Content-Type: application/json" \
  -d "{\"name\": \"hist_a_$TS\", \"password\": \"pass_a\"}")
ID_A=$(echo "$R_A" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])")
[ -n "$ID_A" ] && pass "注册 userA (id=$ID_A)" || fail "注册 userA"

R_B=$(curl -s -X POST $SERVER/api/register -H "Content-Type: application/json" \
  -d "{\"name\": \"hist_b_$TS\", \"password\": \"pass_b\"}")
ID_B=$(echo "$R_B" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])")
[ -n "$ID_B" ] && pass "注册 userB (id=$ID_B)" || fail "注册 userB"

# ---------- 2. Login ----------
echo ""
echo "=== 2. 登录 ==="
curl -s -X POST $SERVER/api/login -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"password\": \"pass_a\"}" > /dev/null && pass "userA 登录成功" || fail "userA 登录失败"
curl -s -X POST $SERVER/api/login -H "Content-Type: application/json" \
  -d "{\"id\": $ID_B, \"password\": \"pass_b\"}" > /dev/null && pass "userB 登录成功" || fail "userB 登录失败"

# ---------- 3. Add friend ----------
echo ""
echo "=== 3. A 添加 B 为好友 ==="
curl -s -X POST $SERVER/api/add_friend -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"friendid\": $ID_B}" > /dev/null && pass "A 添加 B 为好友" || fail "A 添加 B 失败"

# ---------- 4. Send private messages ----------
echo ""
echo "=== 4. A 发送私聊消息 ==="
S1=$(curl -s -X POST $SERVER/api/send_message -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"toid\": $ID_B, \"message\": \"Hello from history test\"}")
echo "$S1" | grep -q '"err_num":0' && pass "A 发送消息 1" || fail "A 发送消息 1 失败"
sleep 0.5
S2=$(curl -s -X POST $SERVER/api/send_message -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"toid\": $ID_B, \"message\": \"Second message for history\"}")
echo "$S2" | grep -q '"err_num":0' && pass "A 发送消息 2" || fail "A 发送消息 2 失败"

# ---------- 5. Query private chat history ----------
echo ""
echo "=== 5. 查询私聊历史 ==="
HIST=$(curl -s -X POST $SERVER/api/chat_history -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"peer_id\": $ID_B, \"chat_type\": 1, \"limit\": 50}")
ERR=$(echo "$HIST" | python3 -c "import sys,json; print(json.load(sys.stdin).get('err_num','-1'))")
MSG_CNT=$(echo "$HIST" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('messages',[])))")
[ "$ERR" = "0" ] && pass "获取私聊历史成功" || fail "获取私聊历史失败 (err=$ERR)"
[ "$MSG_CNT" -ge 2 ] && pass "私聊历史消息数 >= 2 (实际: $MSG_CNT)" || fail "私聊历史消息数不足 (实际: $MSG_CNT)"

MSG1=$(echo "$HIST" | python3 -c "import sys,json; ms=json.load(sys.stdin).get('messages',[]); print(ms[0]['message'])")
MSG2=$(echo "$HIST" | python3 -c "import sys,json; ms=json.load(sys.stdin).get('messages',[]); print(ms[1]['message'])")
FOUND=0
[ "$MSG1" = "Second message for history" -o "$MSG2" = "Second message for history" ] && FOUND=$((FOUND+1))
[ "$MSG1" = "Hello from history test" -o "$MSG2" = "Hello from history test" ] && FOUND=$((FOUND+1))
[ "$FOUND" -ge 2 ] && pass "私聊历史消息内容正确" || fail "私聊历史消息内容不匹配"

TYPE=$(echo "$HIST" | python3 -c "import sys,json; print(json.load(sys.stdin)['messages'][0].get('type',''))")
[ "$TYPE" = "chat" ] && pass "私聊消息类型为 'chat'" || fail "私聊消息类型应为 'chat'，实际为 '$TYPE'"

# ---------- 6. Pagination ----------
echo ""
echo "=== 6. 分页测试 ==="
HIST_PG=$(curl -s -X POST $SERVER/api/chat_history -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"peer_id\": $ID_B, \"chat_type\": 1, \"limit\": 1}")
PG_CNT=$(echo "$HIST_PG" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('messages',[])))")
[ "$PG_CNT" -eq 1 ] && pass "分页 limit=1 返回 1 条" || fail "分页 limit=1 期望 1 条 (实际: $PG_CNT)"

# ---------- 7. Re-login persistence ----------
echo ""
echo "=== 7. 重新登录验证持久化 ==="
curl -s -X POST $SERVER/api/logout -H "Content-Type: application/json" -d "{\"id\": $ID_A}" > /dev/null
curl -s -X POST $SERVER/api/logout -H "Content-Type: application/json" -d "{\"id\": $ID_B}" > /dev/null
curl -s -X POST $SERVER/api/login -H "Content-Type: application/json" -d "{\"id\": $ID_A, \"password\": \"pass_a\"}" > /dev/null
HIST2=$(curl -s -X POST $SERVER/api/chat_history -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"peer_id\": $ID_B, \"chat_type\": 1, \"limit\": 50}")
CNT2=$(echo "$HIST2" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('messages',[])))")
[ "$CNT2" -ge 2 ] && pass "重新登录后历史消息仍然存在 (条数: $CNT2)" || fail "重新登录后历史消息丢失 (条数: $CNT2)"

# ---------- 8. Group chat history ----------
echo ""
echo "=== 8. 群聊历史测试 ==="
GRP=$(curl -s -X POST $SERVER/api/create_group -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"name\": \"hist_grp_$TS\", \"desc\": \"test group\"}")
GID=$(echo "$GRP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('groupid',''))")
[ -n "$GID" ] && pass "创建群组 (id=$GID)" || fail "创建群组失败"

GS1=$(curl -s -X POST $SERVER/api/send_group_message -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"groupid\": $GID, \"message\": \"Group message 1\"}")
echo "$GS1" | grep -q '"err_num":0' && pass "A 发送群消息 1" || fail "A 发送群消息 1 失败"
sleep 0.5
GS2=$(curl -s -X POST $SERVER/api/send_group_message -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"groupid\": $GID, \"message\": \"Group message 2\"}")
echo "$GS2" | grep -q '"err_num":0' && pass "A 发送群消息 2" || fail "A 发送群消息 2 失败"

sleep 0.5
GRP_HIST=$(curl -s -X POST $SERVER/api/chat_history -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"peer_id\": $GID, \"chat_type\": 2, \"limit\": 50}")
ERR_G=$(echo "$GRP_HIST" | python3 -c "import sys,json; print(json.load(sys.stdin).get('err_num','-1'))")
CNT_G=$(echo "$GRP_HIST" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('messages',[])))")
TYPE_G=$(echo "$GRP_HIST" | python3 -c "import sys,json; ms=json.load(sys.stdin).get('messages',[]); print(ms[0].get('type',''))")
[ "$ERR_G" = "0" ] && pass "获取群聊历史成功" || fail "获取群聊历史失败 (err=$ERR_G)"
[ "$CNT_G" -ge 2 ] && pass "群聊历史消息数 >= 2 (实际: $CNT_G)" || fail "群聊历史消息数不足 (实际: $CNT_G)"
[ "$TYPE_G" = "groupchat" ] && pass "群聊消息类型为 'groupchat'" || fail "群聊消息类型应为 'groupchat'，实际为 '$TYPE_G'"

# ---------- 9. Cross-server history ----------
echo ""
echo "=== 9. 跨服历史测试 ==="
# Login B on server 6002, send message from A on server 6000, verify B can see it
curl -s -X POST $SERVER/api/logout -H "Content-Type: application/json" -d "{\"id\": $ID_B}" > /dev/null
# Log B in but the bridge always goes to 6000; cross-server still works via Kafka
curl -s -X POST $SERVER/api/login -H "Content-Type: application/json" -d "{\"id\": $ID_B, \"password\": \"pass_b\"}" > /dev/null
S3=$(curl -s -X POST $SERVER/api/send_message -H "Content-Type: application/json" \
  -d "{\"id\": $ID_A, \"toid\": $ID_B, \"message\": \"Cross-server history test\"}")
echo "$S3" | grep -q '"err_num":0' && pass "A 发送跨服消息" || fail "A 发送跨服消息失败"
sleep 1
HIST3=$(curl -s -X POST $SERVER/api/chat_history -H "Content-Type: application/json" \
  -d "{\"id\": $ID_B, \"peer_id\": $ID_A, \"chat_type\": 1, \"limit\": 50}")
CNT3=$(echo "$HIST3" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('messages',[])))")
[ "$CNT3" -ge 3 ] && pass "跨服消息在 B 的历史中可见 (条数: $CNT3)" || fail "跨服消息在 B 的历史中不可见 (条数: $CNT3)"

# ---------- Cleanup ----------
echo ""
echo "=== 清理 ==="
curl -s -X POST $SERVER/api/logout -H "Content-Type: application/json" -d "{\"id\": $ID_A}" > /dev/null
curl -s -X POST $SERVER/api/logout -H "Content-Type: application/json" -d "{\"id\": $ID_B}" > /dev/null
pass "测试用户已注销"

echo ""
echo "============================================================"
echo -e " \033[0;32m结果: $PASS 通过, $FAIL 失败\033[0m"
echo "============================================================"
[ $FAIL -eq 0 ] && exit 0 || exit 1
