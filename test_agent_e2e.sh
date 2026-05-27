#!/bin/bash
# ============================================================
# AI Agent 端到端功能测试
# 测试 AI 智能助手(ID:10000)的完整功能
# ============================================================

set -e

PASS=0
FAIL=0
MYSQL_CMD="docker compose exec -T mysql-master mysql -uroot -p'Sf523416&111' chat 2>&1 | grep -v Warning | grep -v \"Using a password\""

BRIDGE="http://127.0.0.1:8000"

pass()  { PASS=$((PASS+1)); echo -e "  \033[0;32mPASS\033[0m: $1"; }
fail()  { FAIL=$((FAIL+1)); echo -e "  \033[0;31mFAIL\033[0m: $1"; }

cleanup() {
    docker compose exec -T mysql-master mysql -uroot -p'Sf523416&111' chat -e "
        DELETE FROM offlinemessage WHERE userid IN (SELECT id FROM user WHERE name LIKE 'agent_test_%');
        DELETE FROM friend WHERE userid IN (SELECT id FROM user WHERE name LIKE 'agent_test_%') OR friendid IN (SELECT id FROM user WHERE name LIKE 'agent_test_%');
        DELETE FROM groupuser WHERE userid IN (SELECT id FROM user WHERE name LIKE 'agent_test_%');
        DELETE FROM user WHERE name LIKE 'agent_test_%';
    " 2>&1 | grep -v Warning | grep -v "Using a password" > /dev/null 2>&1 || true
}

cleanup

echo "============================================================"
echo " AI Agent 端到端测试 $(date)"
echo "============================================================"
echo ""

echo "=== 1. 注册测试用户 ==="
A_NAME="agent_test_a_$(date +%s)"
B_NAME="agent_test_b_$(date +%s)"
A_JSON=$(curl -s -X POST "$BRIDGE/api/register" -H "Content-Type: application/json" -d "{\"name\":\"$A_NAME\",\"password\":\"123\"}")
A_ID=$(echo "$A_JSON" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])" 2>/dev/null || echo "")
[ -n "$A_ID" ] && pass "注册 userA (id=$A_ID)" || fail "注册 userA 失败"
B_JSON=$(curl -s -X POST "$BRIDGE/api/register" -H "Content-Type: application/json" -d "{\"name\":\"$B_NAME\",\"password\":\"123\"}")
B_ID=$(echo "$B_JSON" | python3 -c "import sys,json; print(json.load(sys.stdin)['user']['id'])" 2>/dev/null || echo "")
[ -n "$B_ID" ] && pass "注册 userB (id=$B_ID)" || fail "注册 userB 失败"

echo ""
echo "=== 2. 登录后验证好友列表包含 AI 助手 ==="
LOGIN_A=$(curl -s -X POST "$BRIDGE/api/login" -H "Content-Type: application/json" -d "{\"id\":$A_ID,\"password\":\"123\"}")
echo "$LOGIN_A" | python3 -c "
import sys, json
data = json.load(sys.stdin)
friends = data.get('friends', [])
ai_ids = [f['id'] for f in friends if f['id'] == 10000]
groups = data.get('groups', [])
ai_name = [f['name'] for f in friends if f['id'] == 10000]
print(f'  好友数: {len(friends)}, 群组数: {len(groups)}, AI名称: {ai_name}')
assert len(ai_ids) > 0, 'AI 助手不在好友列表中'
" && pass "好友列表包含 AI 智能助手" || fail "好友列表缺少 AI 智能助手"

echo "$LOGIN_A" | python3 -c "
import sys, json
data = json.load(sys.stdin)
groups = data.get('groups', [])
assert any('公共聊天室' in g.get('name','') for g in groups), '缺少公共聊天室群组'
" && pass "群组列表包含公共聊天室" || fail "缺少公共聊天室群组"

echo "$LOGIN_A" | python3 -c "
import sys, json
data = json.load(sys.stdin)
ai_name = [f['name'] for f in data.get('friends', []) if f['id'] == 10000][0]
# 验证 AI 名称不是乱码（不含问号、不含特殊替换字符、可被正确解码）
try:
    name_check = ai_name.encode('latin1').decode('utf-8')
    print(f'  AI名称被双重编码了: {name_check}')
    sys.exit(1)
except (UnicodeEncodeError, UnicodeDecodeError):
    # 已经是正确编码
    pass
assert 'AI' in ai_name and '智能' in ai_name, f'AI名称乱码: {ai_name}'
print(f'  AI名称正确: {ai_name}')
" && pass "AI 名称显示正确（无乱码）" || fail "AI 名称乱码"

echo ""
echo "=== 3. 给 AI 助手发消息 ==="
SEND_JSON=$(curl -s -X POST "$BRIDGE/api/send_message" -H "Content-Type: application/json" -d "{\"id\":$A_ID,\"toid\":10000,\"message\":\"你好，请问你叫什么名字？\"}")
echo "$SEND_JSON" | python3 -c "
import sys, json
data = json.load(sys.stdin)
assert data.get('err_num') == 0, f'发送失败: {data}'
" && pass "发送消息给 AI 助手" || fail "发送消息失败"

echo ""
echo "=== 4. 等待 AI 回复 ==="
sleep 5
HISTORY=$(curl -s -X POST "$BRIDGE/api/chat_history" -H "Content-Type: application/json" -d "{\"id\":$A_ID,\"peer_id\":10000,\"chat_type\":1,\"limit\":10}")
echo "$HISTORY" | python3 -c "
import sys, json
data = json.load(sys.stdin)
msgs = data.get('messages', [])
replies = [m for m in msgs if m.get('fromid') == 10000]
assert len(replies) > 0, 'AI 没有回复'
reply = replies[-1]['message']
assert len(reply) > 0, '回复内容为空'
print(f'  AI 回复内容: {reply}')
" && pass "AI 回复了消息" || fail "AI 未回复消息"

echo ""
echo "=== 5. 验证跨服消息投递 ==="
SEND_B=$(curl -s -X POST "$BRIDGE/api/send_message" -H "Content-Type: application/json" -d "{\"id\":$A_ID,\"toid\":$B_ID,\"message\":\"hello from agent test\"}")
echo "$SEND_B" | python3 -c "
import sys, json
data = json.load(sys.stdin)
assert data.get('err_num') == 0
" && pass "跨用户消息发送成功" || fail "跨用户消息发送失败"

echo ""
echo "=== 6. 验证离线消息分页 ==="
sleep 1
PAGE1=$(curl -s -X POST "$BRIDGE/api/chat_history" -H "Content-Type: application/json" -d "{\"id\":$A_ID,\"peer_id\":10000,\"chat_type\":1,\"limit\":1}")
CNT1=$(echo "$PAGE1" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('messages',[])))" 2>/dev/null || echo "0")
[ "$CNT1" -eq 1 ] && pass "分页 limit=1 返回 1 条" || fail "分页返回 $CNT1 条（期望 1）"

echo ""
echo "=== 7. 注销 ==="
LOGOUT=$(curl -s -X POST "$BRIDGE/api/logout" -H "Content-Type: application/json" -d "{\"id\":$A_ID}")
echo "$LOGOUT" | python3 -c "
import sys, json
assert json.load(sys.stdin).get('err_num') == 0
" && pass "userA 注销成功" || fail "userA 注销失败"

echo ""
echo "============================================================"
TOTAL=$((PASS+FAIL))
if [ $FAIL -eq 0 ]; then
    echo -e " \033[0;32m全部 $TOTAL 个 Agent 测试通过！\033[0m"
else
    echo -e " \033[0;33m$PASS/$TOTAL 通过，$FAIL 个失败\033[0m"
fi
echo "============================================================"

cleanup
exit $FAIL
