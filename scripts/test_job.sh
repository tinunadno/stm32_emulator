#!/usr/bin/env bash
set -e

KEYDB_PORT="${KEYDB_PORT:-6379}"
BINARY="${1:-examples/firmware.bin}"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: firmware not found: $BINARY"
    echo "Usage: $0 [path/to/firmware.bin]"
    exit 1
fi

if ! command -v redis-cli &>/dev/null; then
    echo "ERROR: redis-cli not found (install redis-tools)"
    exit 1
fi

if ! redis-cli -p "$KEYDB_PORT" PING &>/dev/null; then
    echo "ERROR: cannot reach KeyDB on port $KEYDB_PORT"
    echo "Run 'make up' first"
    exit 1
fi

BIN_B64=$(base64 -w0 "$BINARY")
JOB_ID="test-$(date +%s)"
SUBMITTED_AT=$(date -u +%FT%TZ)

echo "Submitting job $JOB_ID ($(wc -c < "$BINARY") bytes)..."

redis-cli -p "$KEYDB_PORT" LPUSH sim:jobs:pending \
    "{\"id\":\"$JOB_ID\",\"binary_b64\":\"$BIN_B64\",\"submitted_at\":\"$SUBMITTED_AT\"}" \
    > /dev/null

echo "Waiting for result (up to 60 s)..."
for i in $(seq 1 60); do
    RESULT=$(redis-cli -p "$KEYDB_PORT" GET "sim:results:$JOB_ID")
    if [ -n "$RESULT" ]; then
        echo ""
        echo "=== Result ==="
        echo "$RESULT" | python3 -m json.tool
        echo ""
        echo "=== Stats ==="
        redis-cli -p "$KEYDB_PORT" HGETALL sim:stats
        exit 0
    fi
    printf "."
    sleep 1
done

echo ""
echo "ERROR: timed out waiting for result"
exit 1
