#!/bin/bash
# MySQL 从库自动复制配置脚本
# 由 mysql-slave1/2 容器在首次初始化时自动执行（挂载到 /docker-entrypoint-initdb.d/）。
# 前提：主库已通过 healthcheck（意味着主库 init.sql 已执行完毕、binlog 就绪），
#       从库此前已重放相同的 01-init.sql，基础数据与主库一致，
#       因此从主库"当前" binlog 位点开始复制即可保持同步。
set -e

MASTER_HOST="${MASTER_HOST:-mysql-master}"
MASTER_PORT="${MASTER_PORT:-3306}"
REPL_USER="${REPL_USER:-repl}"
REPL_PASSWORD="${REPL_PASSWORD:-repl_pass_123}"
ROOT_PASSWORD="${MYSQL_ROOT_PASSWORD:-Sf523416&111}"

echo "[replication] Waiting for master ($MASTER_HOST:$MASTER_PORT) to be reachable..."
for i in $(seq 1 60); do
    if mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -uroot -p"$ROOT_PASSWORD" -e "SELECT 1" >/dev/null 2>&1; then
        echo "[replication] Master is reachable."
        break
    fi
    if [ "$i" = "60" ]; then
        echo "[replication] ERROR: master not reachable after 120s, skip replication setup"
        exit 1
    fi
    sleep 2
done

echo "[replication] Master is reachable. Ensuring replication user exists..."
# 兼容旧数据卷：老主库可能没有执行过含 repl 用户的 init.sql，这里确保账号存在
mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -uroot -p"$ROOT_PASSWORD" <<SQL
    CREATE USER IF NOT EXISTS '$REPL_USER'@'%' IDENTIFIED WITH caching_sha2_password BY '$REPL_PASSWORD';
    GRANT REPLICATION SLAVE ON *.* TO '$REPL_USER'@'%';
    FLUSH PRIVILEGES;
SQL

# 获取主库当前 binlog 位点（此位点在主库 init.sql 执行完成之后）
# 从库已重放相同的 01-init.sql，基础数据与主库一致，从当前位点开始复制即可
LOG_FILE=$(mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -uroot -p"$ROOT_PASSWORD" \
    -e "SHOW MASTER STATUS\G" | grep 'File:' | awk '{print $2}')
LOG_POS=$(mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -uroot -p"$ROOT_PASSWORD" \
    -e "SHOW MASTER STATUS\G" | grep 'Position:' | awk '{print $2}')

if [ -z "$LOG_FILE" ] || [ -z "$LOG_POS" ]; then
    echo "[replication] ERROR: cannot read master binlog position"
    exit 1
fi
echo "[replication] Master binlog: $LOG_FILE @ $LOG_POS"

# 配置从库连接主库（本地 root 经 socket 执行，无需密码）
# GET_MASTER_PUBLIC_KEY=1: MySQL 8.0 caching_sha2_password 认证所需
mysql -uroot <<SQL
    STOP SLAVE;
    CHANGE MASTER TO
        MASTER_HOST='$MASTER_HOST',
        MASTER_PORT=$MASTER_PORT,
        MASTER_USER='$REPL_USER',
        MASTER_PASSWORD='$REPL_PASSWORD',
        MASTER_LOG_FILE='$LOG_FILE',
        MASTER_LOG_POS=$LOG_POS,
        GET_MASTER_PUBLIC_KEY=1;
    START SLAVE;
SQL

# 验证复制状态
sleep 3
SLAVE_IO=$(mysql -uroot -e "SHOW SLAVE STATUS\G" | grep 'Slave_IO_Running:' | awk '{print $2}')
SLAVE_SQL=$(mysql -uroot -e "SHOW SLAVE STATUS\G" | grep 'Slave_SQL_Running:' | awk '{print $2}')

if [ "$SLAVE_IO" = "Yes" ] && [ "$SLAVE_SQL" = "Yes" ]; then
    echo "[replication] MySQL replication started successfully! IO: Yes / SQL: Yes"
else
    echo "[replication] WARNING: replication not fully running. IO: $SLAVE_IO / SQL: $SLAVE_SQL"
    mysql -uroot -e "SHOW SLAVE STATUS\G" | grep -E 'Last_IO_Error|Last_SQL_Error' || true
fi
