#!/usr/bin/env python3
"""Claude Code hooks that keep IDA work flowing into the repo (.claude/settings.json; docs/re/README.md, The journal).

  start  SessionStart: which IDA instance has the shared database, and this branch's RE so far
  pre    PreToolUse on ida-multi-mcp: refuses an instance that has another database open (its work would be lost)
  post   PostToolUse: journals IDA reads and writes; reminds (never blocks) when RE falls behind

A hook that fails for any other reason stays silent: it must never get in the agent's way."""
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]
import journal  # noqa: E402
from address_library import load as load_address_library  # noqa: E402
from skyrim_runtime import library_name, runtime_version  # noqa: E402

IDA = "mcp__ida-multi-mcp__"
REGISTRY = Path(os.environ.get("IDA_MCP_REGISTRY", Path.home() / ".ida-mcp/instances.json"))
DEBT_STEP = 3  # remind at every third unnamed function read and not yet documented
DUO_TOOLS = ("mcp__skyrim-coop__duo_start", "mcp__skyrim-coop__vanilla_start")
DUO_COMMAND = re.compile(r"coop_test\.py\s+(duo|scenario|repro|launch)\b")
ENGINE_CODE = re.compile(r"^Code/client/(?!TestHarness/|Ui/|HotReload/)")
PY_EVAL_WRITE = re.compile(r"\b(set_name|set_cmt|set_func_cmt|apply_tinfo|SetType|parse_decls?|set_lvar|rename_lvar)\b")


def git(root, *arguments):
    return subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True).stdout.strip()


class Checkout:
    """The session's checkout: its branch's journal and the shared database every checkout uses."""

    def __init__(self, root):
        self.root = Path(root)
        self.version = runtime_version().removesuffix(".0")
        branch = git(self.root, "rev-parse", "--abbrev-ref", "HEAD") or "unknown"
        if branch == "HEAD":
            branch = "detached-" + git(self.root, "rev-parse", "--short", "HEAD")
        self.branch = branch
        self.journal = journal.file_for(self.root, self.version, branch)
        main = Path(git(self.root, "rev-parse", "--path-format=absolute", "--git-common-dir") or self.root / ".git").parent
        self.database = Path(os.environ.get("COOP_RE_IDB", main / "build/re" / f"SkyrimSE_{self.version}.i64")).resolve()

    def rows(self):
        try:
            return journal.load(self.journal)
        except (ValueError, json.JSONDecodeError):
            return []

    def ids(self):
        """RVA -> Address Library ID; empty without the library (the row keeps its RVA)."""
        candidates = [self.root / "build/runtime-vendor" / self.version / "Data/SKSE/Plugins" / library_name()]
        if "COOP_RE_LIBRARY" in os.environ:
            candidates.insert(0, Path(os.environ["COOP_RE_LIBRARY"]))
        for path in candidates:
            if path.exists():
                return {rva: ident for ident, rva in load_address_library(path).items()}
        return {}

    def instances(self):
        """{instance id: database path} from ida-multi-mcp's registry."""
        try:
            registered = json.loads(REGISTRY.read_text()).get("instances", {})
        except (OSError, json.JSONDecodeError):
            return {}
        return {key: value.get("idb_path", "") for key, value in registered.items()
                if not value.get("pid") or Path(f"/proc/{value['pid']}").exists()}  # a killed IDA stays listed

    def shared_instance(self):
        return next((key for key, path in self.instances().items() if path and Path(path).resolve() == self.database), None)


# --- Turning a tool call into journal rows ---

def address(value):
    if value is None:
        return None
    try:
        ea = value if isinstance(value, int) else int(str(value).strip(), 0)  # as ida-multi-mcp's parse_address
    except ValueError:
        return None
    return ea - journal.IMAGE_BASE if ea >= journal.IMAGE_BASE else ea


def listed(value):
    return [] if value is None else value if isinstance(value, list) else [value]


def succeeded(result):
    """A result item that reports no error (unknown shapes count: better a replayed no-op than a lost note)."""
    if isinstance(result, dict) and result.get("ok") is True:
        return True  # set_comments: ok with an error when only the decompiler view missed it
    return not (isinstance(result, dict) and (result.get("error") or result.get("ok") is False))


def function_name(tool, response):
    """The name the read showed: disasm reports it; decompile's signature line carries it."""
    try:
        parsed = json.loads(response) if isinstance(response, str) else response
    except json.JSONDecodeError:
        parsed = None
    if tool == "disasm" and isinstance(parsed, dict):
        return (parsed.get("asm") or {}).get("name", "")
    code = parsed.get("code") if isinstance(parsed, dict) else str(response)
    for line in (code or "").splitlines():
        if line.strip() and not line.lstrip().startswith("//"):
            match = re.search(r"([A-Za-z_][\w:~<>`'$@?]*)\s*\(", line)
            return match.group(1) if match else ""
    return ""


def parse(response):
    try:
        return json.loads(response) if isinstance(response, str) else response
    except json.JSONDecodeError:
        return None


def items_for(tool, arguments, response):
    """[(op, rva or None, item)] for one ida-multi-mcp call."""
    result = parse(response)
    if tool in ("decompile", "disasm"):
        rva = address(arguments.get("addr"))
        if rva is None or (isinstance(result, dict) and result.get("error")):
            return []
        return [(journal.READ, rva, {"name": function_name(tool, response)})]
    out = []
    if tool == "rename":
        batch = arguments.get("batch") or {}
        for op, (_, category) in journal.WRITES.items():
            if category is None:
                continue
            results = listed(result.get(category)) if isinstance(result, dict) else []
            for index, item in enumerate(listed(batch.get(category))):
                if succeeded(results[index] if index < len(results) else None):
                    out.append((op, address(item.get("addr") or item.get("func_addr")), item))
        return out
    inputs = {"set_comments": ("comment", "items"), "set_type": ("type", "edits"), "declare_type": ("decl", "decls")}
    if tool not in inputs:
        return []
    op, key = inputs[tool]
    if isinstance(result, dict) and set(result) == {"result"}:
        result = result["result"]  # the router wraps a list result as {"result": [...]}
    results = listed(result)
    for index, item in enumerate(listed(arguments.get(key))):
        if succeeded(results[index] if index < len(results) else None):
            item = {"decl": item} if isinstance(item, str) else item
            out.append((op, address(item.get("addr")), item))
    return out


def journal_call(checkout, tool, arguments, response, now=None):
    """Appends the call's rows; a function already read on this branch is not read again. Returns the new rows."""
    items = items_for(tool, arguments, response)
    if not items:
        return []
    existing = checkout.rows()
    seen = {row.rva for row in existing if row.op == journal.READ}
    ids = checkout.ids()
    stamp = now or time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    rows = [journal.Row(stamp, op, ids.get(rva), rva, item) for op, rva, item in items
            if not (op == journal.READ and rva in seen)]
    if rows:
        journal.append(checkout.journal, rows)
    return rows


# --- Reminders ---

class Shown:
    """Reminders already given this session (build/ida-hook/<session>.json)."""

    def __init__(self, checkout, session):
        folder = checkout.root / "build"
        self.path = folder / "ida-hook" / f"{session or 'none'}.json" if folder.is_dir() else None
        try:
            self.kinds = set(json.loads(self.path.read_text())) if self.path else set()
        except (OSError, json.JSONDecodeError):
            self.kinds = set()

    def first(self, kind):
        if kind in self.kinds:
            return False
        self.kinds.add(kind)
        if self.path:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text(json.dumps(sorted(self.kinds)))
        return True


def describe(row):
    return f"{row.item.get('name')} ({row.id or hex(row.rva + journal.IMAGE_BASE)})"


def no_reads(checkout):
    return not any(row.op == journal.READ for row in checkout.rows())


def instance_hint(checkout):
    instance = checkout.shared_instance()
    return f"instance_id {instance}" if instance else f"ask the user to open {checkout.database} in IDA"


def reminders(checkout, event, shown, new_rows):
    tool = event.get("tool_name", "")
    arguments = event.get("tool_input") or {}
    if tool.startswith(IDA):
        name = tool.removeprefix(IDA)
        if any(row.op == journal.READ for row in new_rows):
            debt = journal.undocumented(checkout.rows())
            if debt and len(debt) % DEBT_STEP == 0 and debt[-1] in new_rows:
                return (f"{len(debt)} functions read in IDA on this branch still have auto names and nothing recorded: "
                        f"{', '.join(describe(row) for row in debt)}. Name or comment each with rename / set_comments "
                        "(a comment like 'not on the path: <why>' is fine); the journal carries it into the repo.")
        if name == "py_eval" and PY_EVAL_WRITE.search(str(arguments.get("code", ""))):
            return ("Names, comments and types set through py_eval don't reach the repo's journal. Use rename, "
                    "set_comments, set_type or declare_type for what should be kept.")
        return None
    starts_duo = tool in DUO_TOOLS or (tool == "Bash" and DUO_COMMAND.search(str(arguments.get("command", ""))))
    if starts_duo and no_reads(checkout) and shown.first("duo"):
        return ("No IDA reads on this branch yet. Step 0 comes before the duo (CLAUDE.md, the bug-fix flow): read the "
                f"engine path in IDA ({instance_hint(checkout)}), record names, comments and types as you go, and let "
                "the duo test a stated hypothesis.")
    if tool in ("Edit", "Write", "MultiEdit"):
        path = Path(str(arguments.get("file_path", "")))
        try:
            relative = path.resolve().relative_to(checkout.root.resolve()).as_posix()
        except ValueError:
            return None
        if ENGINE_CODE.match(relative) and no_reads(checkout) and shown.first("edit"):
            return ("You are changing engine-facing code with no IDA reads on this branch. Read the engine functions "
                    f"this code depends on in IDA ({instance_hint(checkout)}) and record what they do, names and "
                    "comments, before relying on them.")
    return None


# --- Modes ---

def start(checkout, _event):
    rows = checkout.rows()
    reads = sum(row.op == journal.READ for row in rows)
    writes = sum(row.op in journal.WRITES for row in rows)
    instance = checkout.shared_instance()
    where = (f"IDA: instance {instance} has the shared database ({checkout.database}); pass instance_id {instance}."
             if instance else f"IDA: no instance has the shared database ({checkout.database}) open; ask the user to "
                              "open it before RE work.")
    print(f"{where} This branch's RE journal ({checkout.journal.relative_to(checkout.root)}): {reads} functions read, "
          f"{writes} names/comments/types written. Step 0 first: read the engine path in IDA and record what you "
          "learn there as you go (rename, set_comments, set_type); the journal carries it into the repo.")
    return 0


def pre(checkout, event):
    arguments = event.get("tool_input") or {}
    instance = arguments.get("instance_id")
    opened = checkout.instances().get(instance) if instance else None
    if not opened or Path(opened).resolve() == checkout.database:
        return 0
    other = checkout.shared_instance()
    use = f"use instance_id {other}" if other else f"ask the user to open {checkout.database} in IDA"
    print(f"ida_hook: instance {instance} has {opened} open, not the shared database {checkout.database}. Work there is "
          f"lost to everyone else: {use}.", file=sys.stderr)
    return 2


def post(checkout, event):
    tool = event.get("tool_name", "")
    new_rows = []
    if tool.startswith(IDA):
        new_rows = journal_call(checkout, tool.removeprefix(IDA), event.get("tool_input") or {},
                                event.get("tool_response"))
    message = reminders(checkout, event, Shown(checkout, event.get("session_id")), new_rows)
    if message:
        print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": message}}))
    return 0


def main(mode):
    try:
        event = json.loads(sys.stdin.read() or "{}")
        checkout = Checkout(os.environ.get("CLAUDE_PROJECT_DIR") or event.get("cwd") or os.getcwd())
        return {"start": start, "pre": pre, "post": post}[mode](checkout, event)
    except Exception as error:  # noqa: BLE001  never in the agent's way
        print(f"ida_hook {mode}: {error}", file=sys.stderr)
        return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "post"))
