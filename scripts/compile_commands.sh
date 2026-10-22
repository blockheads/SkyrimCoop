#!/usr/bin/env bash
# Writes build/compile_commands.json for clangd (see .clangd): the clang-cl plugin build, then the Linux build's
# files it lacks (the tests, coop_peer). Rerun after adding or removing source files.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/compile_commands.json"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# xmake treats the output path as a directory.
"$ROOT/scripts/xmake-msvc.sh" project -k compile_commands "$TMP/windows" >/dev/null
"$ROOT/scripts/xmake-linux.sh" project -k compile_commands "$TMP/linux" >/dev/null
python3 - "$TMP/windows/compile_commands.json" "$TMP/linux/compile_commands.json" "$OUT" <<'PY'
import json, os, sys
windows, linux, out = sys.argv[1:]
entries = json.load(open(windows))
seen = {os.path.join(e["directory"], e["file"]) for e in entries}
entries += [e for e in json.load(open(linux)) if os.path.join(e["directory"], e["file"]) not in seen]
json.dump(entries, open(out, "w"), indent=1)
print(f"{len(entries)} entries in {out}")
PY
