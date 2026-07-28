#!/usr/bin/env bash
# Local 2-worker + coordinator demo for builtin sharding v1.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT}/bin/nobugdb"
DEMO_DIR="${ROOT}/build/demo_shard"
SECRET="demo-secret"
COORD_PORT=9100
W0_PORT=9101
W1_PORT=9102

if [[ ! -x "${BIN}" ]]; then
  echo "Build the server first: make build"
  exit 1
fi

rm -rf "${DEMO_DIR}"
mkdir -p "${DEMO_DIR}/worker0" "${DEMO_DIR}/worker1" "${DEMO_DIR}/coord"

cat > "${DEMO_DIR}/shard_map.conf" <<EOF
# workers
0 127.0.0.1 ${W0_PORT}
1 127.0.0.1 ${W1_PORT}

# placements
sales_2024 0
sales_2025 1
EOF

cleanup() {
  if [[ -n "${W0_PID:-}" ]]; then kill "${W0_PID}" 2>/dev/null || true; fi
  if [[ -n "${W1_PID:-}" ]]; then kill "${W1_PID}" 2>/dev/null || true; fi
  if [[ -n "${COORD_PID:-}" ]]; then kill "${COORD_PID}" 2>/dev/null || true; fi
  wait 2>/dev/null || true
}
trap cleanup EXIT

"${BIN}" --role=worker --shard-id=0 --port="${W0_PORT}" \
  --data-dir="${DEMO_DIR}/worker0" --rpc-secret="${SECRET}" \
  --no-require-auth >/dev/null 2>&1 &
W0_PID=$!
"${BIN}" --role=worker --shard-id=1 --port="${W1_PORT}" \
  --data-dir="${DEMO_DIR}/worker1" --rpc-secret="${SECRET}" \
  --no-require-auth >/dev/null 2>&1 &
W1_PID=$!
sleep 1

"${BIN}" --role=coordinator --port="${COORD_PORT}" \
  --data-dir="${DEMO_DIR}/coord" --shard-map="${DEMO_DIR}/shard_map.conf" \
  --rpc-secret="${SECRET}" --no-require-auth >/dev/null 2>&1 &
COORD_PID=$!
sleep 1

send_query() {
  local sql="$1"
  python3 - <<PY
import socket
s = socket.create_connection(("127.0.0.1", ${COORD_PORT}), timeout=5)
msg = "QUERY|${sql}\n"
s.sendall(msg.encode())
data = s.recv(65536)
print(data.decode(errors="replace").rstrip())
s.close()
PY
}

echo "== DDL broadcast =="
send_query "CREATE TABLE sales (id INT, y INT) PARTITION BY RANGE (y)"
send_query "CREATE TABLE sales_2024 PARTITION OF sales FOR VALUES FROM (2024) TO (2025)"
send_query "CREATE TABLE sales_2025 PARTITION OF sales FOR VALUES FROM (2025) TO (2026)"

echo "== Single-shard INSERT =="
send_query "INSERT INTO sales VALUES (1, 2024)"
send_query "INSERT INTO sales VALUES (2, 2025)"

echo "== Equality SELECT (single shard) =="
send_query "SELECT * FROM sales WHERE y = 2024"

echo "== Scatter-gather SELECT =="
send_query "SELECT * FROM sales"

echo "Demo finished. Cluster torn down on exit."
