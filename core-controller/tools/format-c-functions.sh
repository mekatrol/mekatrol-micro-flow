#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$WORKSPACE_DIR/src"

if command -v python3 >/dev/null 2>&1; then
  exec python3 "$SCRIPT_DIR/format_c_functions.py" "$SOURCE_DIR" "$@"
elif command -v python >/dev/null 2>&1; then
  exec python "$SCRIPT_DIR/format_c_functions.py" "$SOURCE_DIR" "$@"
else
  echo 'Python 3 is required.' >&2
  exit 1
fi
