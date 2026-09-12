name: CI

on:
  push:
    branches: [main, 'feature/**', 'feat/**', 'course/**']
  pull_request:

jobs:
  # ---- C++ 服务端：在 Ubuntu 上完整编译（muduo/protobuf 编译耗时较长） ----
  build-server:
    name: Build C++ ChatServer (Ubuntu)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build ChatServer via Dockerfile.server
        run: docker build -f Dockerfile.server -t chatserver-test .

  # ---- Python 组件：Agent + Bridge 单元测试 ----
  python-tests:
    name: Python unit tests (agent + bridge)
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - name: Install agent dependencies
        run: pip install -q -r agent_service/requirements.txt
      - name: Install bridge dependencies
        run: pip install -q -r frontend/bridge/requirements.txt pytest httpx
      - name: Test agent_service
        run: cd agent_service && python -m pytest tests -q
      - name: Test frontend/bridge
        run: cd frontend/bridge && python -m pytest tests -q

  # ---- Web 前端：TypeScript 编译 + 生产构建 ----
  frontend:
    name: Build React frontend
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: "22"
          cache: npm
          cache-dependency-path: frontend/web/package-lock.json
      - name: Install dependencies
        run: npm ci
        working-directory: frontend/web
      - name: Type-check and build
        run: npm run build
        working-directory: frontend/web

  # ---- Docker Compose 编排配置合法性校验 ----
  compose-validate:
    name: Validate docker-compose.yml
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Validate compose file
        run: docker compose -f docker-compose.yml config -q