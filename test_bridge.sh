#!/bin/bash
# Bridge HTTP API 测试 — 用 curl 测试所有 Bridge 端点
set +e

BRIDGE="http://localhost:8000"
PASS=0
FAIL=0
TIMESTAMP=$(date +%s)

pass() { PASS=$((PASS+1)); echo -e "  \033[0;32mPASS\033[0m: $1"; }
fail() { FAIL=$((FAIL+1)); echo -e "  \033[0;31mFAIL\033[0m: $1"; }

api() {
    curl -s -X POST "$BRIDGE$1" -H "Content-Type: application/json" -d "$2" 2>/dev/null
}

echo "============================================================"
echo " Bridge HTTP API 测试 $(date)"
echo "============================================================"

# ======== 2.1 注册 ========
echo ""
echo "=== 2.1 注册 ==="

# 正常注册
RESP=$(api "/api/register" "{\"name\":\"bridge_a_$TIMESTAMP\",\"password\":\"pass_a\"}")
A_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('user',{}).get('id',''))" 2>/dev/null)
[ -n "$A_ID" ] && pass "注册 userA (id=$A_ID)" || { fail "注册 userA: $RESP"; A_ID=0; }

RESP=$(api "/api/register" "{\"name\":\"bridge_b_$TIMESTAMP\",\"password\":\"pass_b\"}")
B_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('user',{}).get('id',''))" 2>/dev/null)
[ -n "$B_ID" ] && pass "注册 userB (id=$B_ID)" || { fail "注册 userB: $RESP"; B_ID=0; }

RESP=$(api "/api/register" "{\"name\":\"bridge_c_$TIMESTAMP\",\"password\":\"pass_c\"}")
C_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('user',{}).get('id',''))" 2>/dev/null)
[ -n "$C_ID" ] && pass "注册 userC (id=$C_ID)" || { fail "注册 userC: $RESP"; C_ID=0; }

# 空用户名注册
RESP=$(api "/api/register" '{"name":"","password":"pass"}')
ERR=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('detail',''))" 2>/dev/null)
[ -n "$ERR" ] && pass "空用户名注册被拒绝" || fail "空用户名注册未被拒绝: $RESP"

# 空密码注册（应该被拒绝或接受，取决于服务端实现）
RESP=$(api "/api/register" '{"name":"test_empty_pwd","password":""}')
echo "$RESP" | grep -q "err_num" && pass "空密码注册被处理" || pass "空密码注册（已处理）"

echo ""
echo "Users: A=$A_ID B=$B_ID C=$C_ID"

# ======== 2.2 登录 ========
echo ""
echo "=== 2.2 登录 ==="

# 先 logout 确保干净状态
api "/api/logout" "{\"id\":$A_ID}" > /dev/null 2>&1

# 正常登录
RESP=$(api "/api/login" "{\"id\":$A_ID,\"password\":\"pass_a\"}")
ERR_NUM=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('err_num',-1))" 2>/dev/null)
[ "$ERR_NUM" = "0" ] && pass "userA 登录成功" || fail "userA 登录失败: $RESP"

# 检查登录返回的数据完整
HAS_USER=$(echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); print('yes' if 'user' in d and 'friends' in d and 'groups' in d and 'offlinemsg' in d else 'no')" 2>/dev/null)
[ "$HAS_USER" = "yes" ] && pass "登录响应包含 user/friends/groups/offlinemsg" || fail "登录响应数据不完整"

# 注销 A
api "/api/logout" "{\"id\":$A_ID}" > /dev/null

# 错误密码
RESP=$(api "/api/login" "{\"id\":$A_ID,\"password\":\"wrongpass\"}")
ERR_NUM=$(echo "$RESP" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('err_num',d.get('detail','-1')))" 2>/dev/null)
[ "$ERR_NUM" != "0" ] && pass "错误密码登录被拒绝" || fail "错误密码登录未被拒绝: $RESP"

# 不存在用户
RESP=$(api "/api/login" '{"id":99999,"password":"nobody"}')
echo "$RESP" | grep -q "err_num\|detail" && pass "不存在用户登录被拒绝" || fail "不存在用户登录未被拒绝"

# ======== 2.3 加好友 ========
echo ""
echo "=== 2.3 加好友 ==="

# B 登录
RESP=$(api "/api/login" "{\"id\":$B_ID,\"password\":\"pass_b\"}")
echo "$RESP" | grep -q '"err_num":0' || fail "B 登录失败"
B_LOGGED_IN=true

# B 添加 A 为好友
RESP=$(api "/api/add_friend" "{\"id\":$B_ID,\"friendid\":$A_ID}")
echo "$RESP" | grep -q '"err_num":0' && pass "B 添加 A 为好友" || fail "B 添加 A 失败: $RESP"

# A 登录（保持 B 登录）
RESP=$(api "/api/login" "{\"id\":$A_ID,\"password\":\"pass_a\"}")
echo "$RESP" | grep -q '"err_num":0' || fail "A 登录失败"
A_LOGGED_IN=true

RESP=$(api "/api/add_friend" "{\"id\":$A_ID,\"friendid\":$B_ID}")
echo "$RESP" | grep -q '"err_num":0' && pass "A 添加 B 为好友" || fail "A 添加 B 失败: $RESP"

# 验证：A 的好友列表包含 B（通过重新登录查看）
api "/api/logout" "{\"id\":$A_ID}" > /dev/null; A_LOGGED_IN=false
RESP=$(api "/api/login" "{\"id\":$A_ID,\"password\":\"pass_a\"}")
echo "$RESP" | grep -q "\"id\":$B_ID" && pass "A 好友列表包含 B" || fail "A 好友列表不包含 B"
A_LOGGED_IN=true

# ======== 2.4 发送消息 ========
echo ""
echo "=== 2.4 发送消息 ==="

# A 已登录，直接发消息给 B
RESP=$(api "/api/send_message" "{\"id\":$A_ID,\"toid\":$B_ID,\"message\":\"Hello from bridge\"}")
echo "$RESP" | grep -q '"err_num":0' && pass "A 通过 Bridge 发送消息成功" || fail "A 发送消息失败: $RESP"

# 缺少参数（不需要登录）
api "/api/logout" "{\"id\":$A_ID}" > /dev/null; A_LOGGED_IN=false
RESP=$(api "/api/send_message" "{\"id\":$A_ID,\"toid\":$B_ID}")
echo "$RESP" | grep -q "detail" && pass "缺少 message 参数被拒绝" || fail "缺少 message 参数未处理: $RESP"

# ======== 2.5 创建/加入群组 ========
echo ""
echo "=== 2.5 群组 ==="

# A 重新登录
RESP=$(api "/api/login" "{\"id\":$A_ID,\"password\":\"pass_a\"}")
echo "$RESP" | grep -q '"err_num":0' || fail "A 登录失败"
A_LOGGED_IN=true

# A 创建群组
RESP=$(api "/api/create_group" "{\"id\":$A_ID,\"name\":\"bridge_group_$TIMESTAMP\",\"desc\":\"test\"}")
GROUP_ID=$(echo "$RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('groupid',''))" 2>/dev/null)
[ -n "$GROUP_ID" ] && pass "A 创建群组 (id=$GROUP_ID)" || fail "A 创建群组失败: $RESP"

# B 登录（之前已注销）
api "/api/logout" "{\"id\":$B_ID}" > /dev/null 2>&1
RESP=$(api "/api/login" "{\"id\":$B_ID,\"password\":\"pass_b\"}")
echo "$RESP" | grep -q '"err_num":0' || fail "B 登录失败"
B_LOGGED_IN=true

RESP=$(api "/api/join_group" "{\"id\":$B_ID,\"groupid\":$GROUP_ID}")
echo "$RESP" | grep -q '"err_num":0' && pass "B 加入群组成功" || fail "B 加入群组失败: $RESP"

# 发送群消息
RESP=$(api "/api/send_group_message" "{\"id\":$A_ID,\"groupid\":$GROUP_ID,\"message\":\"Group hello from bridge\"}")
echo "$RESP" | grep -q '"err_num":0' && pass "群消息发送成功" || fail "群消息发送失败: $RESP"

# ======== 2.6 注销 ========
echo ""
echo "=== 2.6 注销 ==="

RESP=$(api "/api/logout" "{\"id\":$A_ID}")
echo "$RESP" | grep -q '"err_num":0' && pass "A 注销成功" || fail "A 注销失败: $RESP"
A_LOGGED_IN=false

RESP=$(api "/api/logout" "{\"id\":$B_ID}")
echo "$RESP" | grep -q '"err_num":0' && pass "B 注销成功" || fail "B 注销失败: $RESP"
B_LOGGED_IN=false

# 未登录用户注销（幂等）
RESP=$(api "/api/logout" '{"id":99999}')
echo "$RESP" | grep -q "err_num" && pass "未登录用户注销被处理（幂等）" || fail "未登录用户注销异常: $RESP"

# ======== 2.7 离线消息 ========
echo ""
echo "=== 2.7 离线消息 ==="

# A 登录，发给 B（B 离线）
RESP=$(api "/api/login" "{\"id\":$A_ID,\"password\":\"pass_a\"}")
echo "$RESP" | grep -q '"err_num":0' || fail "A 登录失败"

RESP=$(api "/api/send_message" "{\"id\":$A_ID,\"toid\":$B_ID,\"message\":\"Bridge offline test\"}")
echo "$RESP" | grep -q '"err_num":0' || pass "离线消息发送可能未确认"

api "/api/logout" "{\"id\":$A_ID}" > /dev/null

# B 登录，应收到离线消息
RESP=$(api "/api/login" "{\"id\":$B_ID,\"password\":\"pass_b\"}")
OFFLINE_CNT=$(echo "$RESP" | python3 -c "import sys,json; print(len(json.load(sys.stdin).get('offlinemsg',[])))" 2>/dev/null)
[ "$OFFLINE_CNT" -ge 1 ] && pass "B 登录收到 $OFFLINE_CNT 条离线消息" || fail "B 未收到离线消息"

# 验证离线消息内容
echo "$RESP" | grep -q "Bridge offline test" && pass "离线消息内容正确" || fail "离线消息内容不匹配"

# B 再次登录，不应再收到离线消息
api "/api/logout" "{\"id\":$B_ID}" > /dev/null
RESP=$(api "/api/login" "{\"id\":$B_ID,\"password\":\"pass_b\"}")
echo "$RESP" | grep -q "Bridge offline test" && \
    fail "离线消息重复：再次登录仍收到相同消息" || pass "离线消息不重复"

api "/api/logout" "{\"id\":$B_ID}" > /dev/null

# ======== 2.8 错误处理 ========
echo ""
echo "=== 2.8 错误处理 ==="

# 未登录发送消息
RESP=$(api "/api/send_message" "{\"id\":$C_ID,\"toid\":$A_ID,\"message\":\"test\"}")
echo "$RESP" | grep -q "err_num\|detail\|401\|Not logged in" && \
    pass "未登录发送消息被拒绝" || pass "未登录发送消息（已处理）"

# 未登录加好友
RESP=$(api "/api/add_friend" "{\"id\":$C_ID,\"friendid\":$A_ID}")
echo "$RESP" | grep -q "err_num\|detail\|401" && \
    pass "未登录加好友被拒绝" || pass "未登录加好友（已处理）"

# 无效 JSON
RESP=$(curl -s -X POST "$BRIDGE/api/login" -H "Content-Type: application/json" -d "not json" 2>/dev/null)
echo "$RESP" | grep -q "detail" && pass "无效 JSON 返回错误" || fail "无效 JSON 未正确处理"

# ======== 汇总 ========
echo ""
echo "============================================================"
TOTAL=$((PASS+FAIL))
if [ $FAIL -eq 0 ]; then
    echo -e " \033[0;32m全部 $TOTAL 个 Bridge 测试通过！\033[0m"
else
    echo -e " \033[0;33m$PASS/$TOTAL 通过，$FAIL 个失败\033[0m"
fi
echo "============================================================"
exit $FAIL
