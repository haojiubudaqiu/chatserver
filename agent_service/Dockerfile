FROM python:3.12-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY proto/ proto/
COPY *.py ./

ENV CHAT_SERVER_HOST=chat_server_1
ENV CHAT_SERVER_PORT=6000
ENV MCP_SERVER_URL=http://chat_server_1:8888/mcp
ENV MODELSCOPE_API_KEY=""
ENV TAVILY_API_KEY=""

CMD ["python", "-u", "main.py"]
