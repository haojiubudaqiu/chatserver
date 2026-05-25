# Chat Client - Windows 使用说明

## 系统架构

```
Windows 浏览器                    Ubuntu VM (192.168.32.134)
┌──────────────────┐          ┌─────────────────────────────────────┐
│  React SPA       │──HTTP──→│  Python Bridge (端口 8000)            │
│  (浏览器打开)     │←─WS────│  fastapi + uvicorn                    │
│                   │          │         ↓  TCP+Protobuf              │
│                   │          │  C++ ChatServer (端口 6000/6001/6002)│
│                   │          │  Nginx (端口 7000)                   │
└──────────────────┘          └─────────────────────────────────────┘
```

## 准备工作（一次性）

### 1. 安装 Node.js（Windows）
- 下载 https://nodejs.org/ 最新 LTS 版（18+）
- 安装时勾选"Add to PATH"

### 2. 克隆项目并安装前端依赖

```bash
# 在 Windows 上打开 PowerShell 或 CMD
cd C:\Projects
git clone <你的仓库地址> chatserver
cd chatserver\frontend\web
npm install
```

## 启动服务端（Ubuntu VM）

服务器已由我们维护运行，确认以下服务均正常：
- Docker 容器全部 `Up`（chat_server_1/2/3, mysql, redis, kafka, nginx 等）
- Python Bridge 运行在 `0.0.0.0:8000`

### 验证服务端可达性

在 Windows 上打开 PowerShell，执行：

```powershell
# 将 192.168.32.134 替换为 Ubuntu VM 的实际 IP
curl.exe -X POST http://192.168.32.134:8000/api/register ^
  -H "Content-Type: application/json" ^
  -d "{\"name\":\"win_test\",\"password\":\"win123\"}"
```

应返回类似：`{"err_num":0,"user":{"id":86,"name":"win_test"}}`

## 启动前端（Windows）

```bash
cd chatserver\frontend\web

# 设置 Ubuntu VM 的 IP 地址并启动
set VITE_BRIDGE_URL=http://192.168.32.134:8000
npm run dev
```

或在 PowerShell 中：

```powershell
$env:VITE_BRIDGE_URL="http://192.168.32.134:8000"
npm run dev
```

浏览器自动打开 `http://localhost:5173`，显示登录/注册界面。

> 如果浏览器没自动打开，手动访问 `http://localhost:5173`

## 功能操作说明

### 注册账号
1. 在右侧 **Register** 区域输入用户名和密码
2. 点击 **Register**
3. 提示注册成功并显示你的 User ID（**请记住这个 ID，登录要用**）

### 登录
1. 在左侧 **Login** 区域输入 User ID 和密码
2. 点击 **Login**（密码与注册时一致）
3. 自动进入主界面，左侧显示好友列表和群组列表

### 主界面布局
```
┌──────────────────┬────────────────────────────────────┐
│  用户名 (#77)     │  聊天对象名称                    │
│  [Logout]        │                                    │
│                  │  ┌──────────────────────────┐      │
│  Friends (3)     │  │  消息气泡（对方）         │      │
│  [Filter...]     │  └──────────────────────────┘      │
│  [+ Add ID] [+] │  ┌──────────────────────────┐      │
│  ● Alice #1      │  │  消息气泡（自己）         │      │
│  ● Bob #2        │  └──────────────────────────┘      │
│  ○ Charlie #3    │                                    │
│                  │  [输入消息...]           [Send]    │
│  Groups (2)      │                                    │
│  ◆ Dev Team      │                                    │
│  ◆ General       │                                    │
└──────────────────┴────────────────────────────────────┘
```

- **左侧面板**：好友列表（●在线/○离线）+ 群组列表（◆）
- **右侧面板**：聊天窗口，消息气泡展示

### 添加好友
1. 在 Friends 区域，输入好友的 User ID
2. 点击 **+** 按钮
3. 提示添加成功（需要重新登录才能刷新好友列表）

### 一对一聊天
1. 点击左侧好友列表中的某个好友
2. 右侧切换到聊天窗口
3. 在底部输入框输入消息，按 Enter 或点击 **Send**
4. 消息以气泡形式展示（绿色为自己，白色为对方）
5. 对方在线时实时送达；离线时下次登录自动拉取

### 创建群组
1. 在 Groups 区域，输入群组名称和描述
2. 点击 **+** 按钮
3. 提示创建成功（需要重新登录才能看到）

### 加入群组
1. 在 Groups 区域，输入群组 ID
2. 点击 **Join** 按钮
3. 提示加入成功（需要重新登录才能看到）

### 群聊
1. 点击左侧群组列表中的某个群组
2. 右侧切换到群聊窗口
3. 输入消息发送，群内所有在线成员实时收到

### 离线消息
- 登录时自动拉取离线期间未读消息，显示在聊天窗口中
- 消息按对话分组，好友/群组消息各自独立

### 退出登录
- 点击右上角 **Logout** 按钮
- 返回登录界面

## 故障排除

| 问题 | 原因 | 解决 |
|------|------|------|
| 页面白屏/无法加载 | Node 版本过低 | 确认 Node.js >= 18 |
| 注册返回 502 | 桥接层未运行 | 联系管理员启动 bridge |
| 登录后立刻断开 | 密码错误 | 确认 ID 和密码 |
| `Failed to fetch` | 网络不通 | `ping 192.168.32.134` 测试连通性 |
| WebSocket 连不上 | 端口 8000 被防火墙阻止 | 检查 Windows 防火墙和 Ubuntu ufw |
| 好友/群组列表为空 | 需要重新登录刷新 | Logout → Login |

## 开发说明

### 修改桥接地址
默认桥接地址通过环境变量 `VITE_BRIDGE_URL` 设置：

```bash
# Windows CMD
set VITE_BRIDGE_URL=http://<你的IP>:8000
npm run dev

# PowerShell
$env:VITE_BRIDGE_URL="http://<你的IP>:8000"
npm run dev
```

### 生产构建
```bash
set VITE_BRIDGE_URL=http://192.168.32.134:8000
npm run build
# 产出在 dist/ 目录，可直接部署到任何静态服务器
```
