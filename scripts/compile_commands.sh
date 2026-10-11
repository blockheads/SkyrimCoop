#!/usr/bin/env bash
# Writes build/compile_commands.json from the clang-cl build, for clangd (see .clangd). Rerun after
# adding or removing source files.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/compile_commands.json"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# xmake treats the output path as a directory.
"$ROOT/scripts/xmake-msvc.sh" project -k compile_commands "$TMP" >/dev/null
mv "$TMP/compile_commands.json" "$OUT"
echo "$(python3 -c "import json,sys; print(len(json.load(open(sys.argv[1]))))" "$OUT") entries in $OUT"
