"""Replay the RE journals (docs/re/journal/<version>/*.tsv) into the open IDA database: every branch's names, comments
and types, oldest first, through ida-multi-mcp's own code, then ida_apply.py's annotations around them.

For a new or lost database (make_ida_db.sh runs it): on the shared database it would put older journal text back over
hand edits made since. In IDA: File > Script file... > scripts/re/ida_replay.py."""
import inspect
import os
import site
import sys

import ida_auto
import ida_kernwin
import idc

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("COOP_RE_JOURNAL_ROOT", os.path.dirname(os.path.dirname(HERE)))
sys.path[:0] = [HERE, os.path.join(os.path.dirname(HERE))]
import ida_apply  # noqa: E402
import journal  # noqa: E402

if site.getusersitepackages() not in sys.path:
    sys.path.append(site.getusersitepackages())  # ida-multi-mcp's usual install
from ida_multi_mcp.ida_mcp import api_modify, api_types  # noqa: E402

# The tools without their sync wrapper: this script already runs on IDA's main thread, and the wrapper would wait on it.
TOOLS = {"rename": inspect.unwrap(api_modify.rename), "set_comments": inspect.unwrap(api_modify.set_comments),
         "set_type": inspect.unwrap(api_types.set_type), "declare_type": inspect.unwrap(api_types.declare_type)}


def replay(rows):
    applied = failed = 0
    for row in rows:
        if row.op not in journal.WRITES:
            continue
        tool, category = journal.WRITES[row.op]
        argument = {category: row.item} if category else row.item["decl"] if tool == "declare_type" else row.item
        try:
            result = TOOLS[tool](argument)
        except Exception as error:  # noqa: BLE001  one bad row must not stop the rest
            result = [{"error": str(error)}]
        results = result.get(category, []) if isinstance(result, dict) else result
        if all(item.get("ok") is True or not item.get("error") for item in results):
            applied += 1
        else:
            failed += 1
            print(f"[re] journal row {row.time} {row.op} {row.item}: {results}")
    return applied, failed


def main():
    version = ida_apply.VERSION.removesuffix(".0")
    rows = journal.load_all(ROOT, version)
    ida_apply.main()  # names and functions first: the journal's names then win over them
    applied, failed = replay(rows)
    ida_apply.main()  # comments the replay replaced get their [re] block back
    print(f"[re] replayed {applied} journal writes ({failed} failed) for {version} from {ROOT}")


if __name__ == "__main__":
    batch = os.environ.get("COOP_IDA_BATCH") == "1"
    if batch:
        ida_auto.auto_wait()
    main()
    if batch:
        ida_auto.auto_wait()
        idc.qexit(0)
    else:
        ida_kernwin.refresh_idaview_anyway()
