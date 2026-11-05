"""The RE journal: what each branch read and wrote in IDA, one row per item of an ida-multi-mcp call (docs/re/README.md,
The journal). scripts/re/ida_hook.py writes it, ida_apply.py replays it, build_re_data.py reads its names and comments.

docs/re/journal/<version>/<branch>.tsv: time, op, id, rva, item. `item` is the JSON the tool was given (a write) or
the function's name when read, so a replay passes it back through the same tool code."""
import json
import os
from collections import namedtuple
from pathlib import Path

COLUMNS = ("time", "op", "id", "rva", "item")
READ = "read"
# Each write op and the ida-multi-mcp tool (and rename category) it replays through.
WRITES = {"name": ("rename", "func"), "data": ("rename", "data"), "local": ("rename", "local"),
          "stack": ("rename", "stack"), "comment": ("set_comments", None), "type": ("set_type", None),
          "decl": ("declare_type", None)}
IMAGE_BASE = 0x140000000

Row = namedtuple("Row", COLUMNS)


def file_for(root, version, branch):
    return Path(root) / "docs/re/journal" / version / (branch.replace("/", "-") + ".tsv")


def load(path):
    path = Path(path)
    if not path.exists():
        return []
    rows = []
    for line in path.read_text().splitlines()[1:]:
        parts = line.split("\t")
        if len(parts) != len(COLUMNS):
            raise ValueError(f"{path}: a row with {len(parts)} columns, not {len(COLUMNS)}: {line[:80]}")
        time, op, ident, rva, item = parts
        rows.append(Row(time, op, int(ident) if ident else None, int(rva, 16) if rva else None, json.loads(item)))
    return rows


def load_all(root, version):
    """Every branch's rows, oldest first: the order a replay applies them in."""
    folder = Path(root) / "docs/re/journal" / version
    rows = [row for path in sorted(folder.glob("*.tsv")) for row in load(path)] if folder.is_dir() else []
    return sorted(rows, key=lambda row: row.time)


def append(path, rows):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = "".join("\t".join((row.time, row.op, "" if row.id is None else str(row.id),
                               "" if row.rva is None else f"{row.rva:#x}",
                               json.dumps(row.item, ensure_ascii=False))) + "\n" for row in rows)
    # One write with O_APPEND: two sessions on one branch don't interleave a line.
    descriptor = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_CREAT, 0o644)
    try:
        header = "\t".join(COLUMNS) + "\n" if os.fstat(descriptor).st_size == 0 else ""
        os.write(descriptor, (header + lines).encode())
    finally:
        os.close(descriptor)


def written(rows):
    """The RVAs this journal names, comments or types (a local or stack name counts for its function)."""
    return {row.rva for row in rows if row.op in WRITES and row.rva is not None}


def undocumented(rows):
    """Functions read while they still had an auto name (sub_) and never named or commented since."""
    done = written(rows)
    return [row for row in rows if row.op == READ and str(row.item.get("name", "")).startswith("sub_")
            and row.rva not in done]
