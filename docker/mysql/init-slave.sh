#!/bin/bash
# MySQL 从库初始化脚本
# 在 slave1/slave2 容器首次启动后执行，建立主从复制
# docker compose exec mysql-slave1 bash /docker-entrypoint-initdb.d/init-slave.sh

set -e

MASTER_HOST="mysql-master"
MASTER_PORT="3306"
MASTER_USER="root"
MASTER_PASSWORD="Sf523416&111"

# 等待主库就绪
echo "Waiting for master ($MASTER_HOST:$MASTER_PORT) to be ready..."
for i in $(seq 1 30); do
  if mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -u"$MASTER_USER" -p"$MASTER_PASSWORD" -e "SELECT 1" >/dev/null 2>&1; then
    echo "Master is ready."
    break
  fi
  sleep 2
done

# 获取主库二进制日志位置
LOG_FILE=$(mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -u"$MASTER_USER" -p"$MASTER_PASSWORD" \
  -e "SHOW MASTER STATUS\G" | grep File | awk '{print $2}')
LOG_POS=$(mysql -h"$MASTER_HOST" -P"$MASTER_PORT" -u"$MASTER_USER" -p"$MASTER_PASSWORD" \
  -e "SHOW MASTER STATUS\G" | grep Position | awk '{print $2}')

echo "Master log: $LOG_FILE at position $LOG_POS"

# 配置从库连接主库
mysql -u"$MASTER_USER" -p"$MASTER_PASSWORD" -e "
  STOP SLAVE;
  CHANGE MASTER TO
    MASTER_HOST='$MASTER_HOST',
    MASTER_PORT=$MASTER_PORT,
    MASTER_USER='$MASTER_USER',
    MASTER_PASSWORD='$MASTER_PASSWORD',
    MASTER_LOG_FILE='$LOG_FILE',
    MASTER_LOG_POS=$LOG_POS;
  START SLAVE;
"

# 验证主从复制状态
sleep 2
SLAVE_IO=$(mysql -u"$MASTER_USER" -p"$MASTER_PASSWORD" -e "SHOW SLAVE STATUS\G" | grep "Slave_IO_Running:" | awk '{print $2}')
SLAVE_SQL=$(mysql -u"$MASTER_USER" -p"$MASTER_PASSWORD" -e "SHOW SLAVE STATUS\G" | grep "Slave_SQL_Running:" | awk '{print $2}')

if [ "$SLAVE_IO" = "Yes" ] && [ "$SLAVE_SQL" = "Yes" ]; then
  echo "MySQL replication started successfully! IO: $SLAVE_IO, SQL: $SLAVE_SQL"
else
  echo "WARNING: Replication may not be fully running. IO: $SLAVE_IO, SQL: $SLAVE_SQL"
  mysql -u"$MASTER_USER" -p"$MASTER_PASSWORD" -e "SHOW SLAVE STATUS\G" | grep -E "Last_IO_Error|Last_SQL_Error|Slave_IO_Running|Slave_SQL_Running"
fi
