# Chat Client Frontend

## Architecture

```
Browser (Windows) → React SPA  ─HTTP/WS─→  Python Bridge  ─TCP+Protobuf─→  C++ Chat Server (Ubuntu)
                   (port 5173)              (port 8000)                     (port 6000/6001/6002)
```

- **Bridge** (`bridge/`): Python FastAPI server. Connects to the C++ chat server via the native TCP+Protobuf protocol. Exposes HTTP REST APIs and WebSocket for real-time message push.
- **Web** (`web/`): React + Vite + TypeScript SPA. Runs in any browser on Windows.

## Prerequisites

### Bridge
- Python 3.10+
- `pip install fastapi uvicorn websockets protobuf`

### Web
- Node.js 18+
- `npm install`

## Running

### 1. Start the C++ chat server (Docker)
```bash
cd /path/to/chatserver
docker compose up -d chat_server_1 chat_server_2 chat_server_3
```

### 2. Start the bridge
```bash
cd frontend/bridge
python3 main.py
# Bridge listens on http://0.0.0.0:8000
```

### 3. Start the web frontend
```bash
cd frontend/web
npm run dev
# Open http://localhost:5173 in browser (Windows)
```

## API Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/api/register` | Register new user |
| POST | `/api/login` | Login (returns friends, groups, offline messages) |
| POST | `/api/logout` | Logout |
| POST | `/api/add_friend` | Add friend by ID |
| POST | `/api/send_message` | Send private message |
| POST | `/api/create_group` | Create new group |
| POST | `/api/join_group` | Join group by ID |
| POST | `/api/send_group_message` | Send group chat message |
| WS | `/ws/{user_id}` | WebSocket for real-time push |

## VS Code (Windows) Dev Setup

1. Open `frontend/web` as the workspace folder
2. Terminal: `npm run dev`
3. Start the bridge on the Ubuntu VM: `cd frontend/bridge && python3 main.py`
4. If bridge is on a different machine, update `BRIDGE` constant in `src/App.tsx`
