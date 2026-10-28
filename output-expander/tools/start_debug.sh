#!/usr/bin/env bash

# Run from a copy saved as output-expander/tools/start-debug.sh.
# Starts the project's WCH-compatible OpenOCD and opens GDB against the
# already-flashed matching debug ELF. Does not build, flash, or reset the MCU.
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OPENOCD_BIN="${OPENOCD_BIN:-$HOME/opt/wch-openocd/bin/openocd}"
GDB_BIN="${GDB_BIN:-riscv-none-elf-gdb}"
CONFIG="$PROJECT_DIR/debug/openocd.cfg"
ELF="$PROJECT_DIR/build/ch32v003-debug/output-expander.elf"

if [[ ! -x "$OPENOCD_BIN" ]]; then
    printf 'Error: WCH OpenOCD not executable: %s\n' "$OPENOCD_BIN" >&2
    exit 1
fi
if ! command -v "$GDB_BIN" >/dev/null 2>&1; then
    printf 'Error: GDB not found: %s (check PATH or set GDB_BIN)\n' "$GDB_BIN" >&2
    exit 1
fi
if [[ ! -f "$CONFIG" ]]; then
    printf 'Error: OpenOCD configuration missing: %s\n' "$CONFIG" >&2
    exit 1
fi
if [[ ! -f "$ELF" ]]; then
    printf 'Error: debug ELF missing: %s\n' "$ELF" >&2
    printf 'Build and flash the debug image first: ./tools/flash.sh ch32v003-debug\n' >&2
    exit 1
fi

# Refuse to compete with an existing OpenOCD server.
if command -v ss >/dev/null 2>&1; then
    if ss -H -ltn 'sport = :3333' | grep -q .; then
        echo 'Error: TCP port 3333 is already in use; stop the existing debug server first.' >&2
        exit 1
    fi
fi

LOG_FILE="$(mktemp /tmp/ch32v003-openocd.XXXXXX.log)"
OCD_PID=''
cleanup() {
    if [[ -n "$OCD_PID" ]] && kill -0 "$OCD_PID" 2>/dev/null; then
        kill "$OCD_PID" 2>/dev/null || true
        wait "$OCD_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'OpenOCD log: %s\n' "$LOG_FILE"
"$OPENOCD_BIN" -f "$CONFIG" >"$LOG_FILE" 2>&1 &
OCD_PID=$!

# Wait for the specific GDB-server-ready message without touching its TCP port.
READY=0
for ((attempt=0; attempt<150; attempt++)); do
    if grep -q 'Listening on port 3333 for gdb connections' "$LOG_FILE"; then
        READY=1
        break
    fi
    if ! kill -0 "$OCD_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done

if [[ "$READY" -ne 1 ]]; then
    echo 'Error: OpenOCD did not start its GDB server. Output:' >&2
    cat "$LOG_FILE" >&2
    exit 1
fi

printf 'Connected debug server on localhost:3333. Starting GDB.\n'
printf 'GDB will attach and stop the target; use "continue" to run it.\n'
printf 'Quit GDB with "quit"; this script will then stop its OpenOCD process.\n\n'
"$GDB_BIN" "$ELF" \
    -ex 'set pagination off' \
    -ex 'target remote localhost:3333'
