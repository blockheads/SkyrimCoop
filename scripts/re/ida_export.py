"""Export RE work done in IDA back into the repo (docs/re/ida_notes.tsv).

In IDA: File > Script file... > scripts/re/ida_export.py, then run
scripts/re/build_re_data.py: your names become the top-priority source and your
comments appear in docs/re/ENGINE_REFERENCE.md and the next ida_apply.py.

Exports every address that has an Address Library ID and either a name you set
(not one ida_apply.py set) or a comment you wrote above the "[re]" marker.
"""
import os
import sys

import ida_bytes
import ida_funcs
import ida_nalt
import ida_name
import ida_netnode
import idautils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import build_re_data  # noqa: E402  (shares the Address Library decoder)

OUT = os.path.join(ROOT, "docs", "re", "ida_notes.tsv")


def user_comment(ea):
    function = ida_funcs.get_func(ea)
    text = ida_funcs.get_func_cmt(function, True) if function and function.start_ea == ea else ida_bytes.get_cmt(ea, True)
    return (text or "").split("[re]")[0].strip()


def main():
    rva_to_id = {rva: ident for ident, rva in build_re_data.load_address_library(build_re_data.VERSIONLIB).items()}
    base = ida_nalt.get_imagebase()
    applied = ida_netnode.netnode("$ skyrimcoop.re", 0, True)
    rows = []
    candidates = {ea for ea, _ in idautils.Names()}
    for ea in sorted(candidates):
        ident = rva_to_id.get(ea - base)
        if ident is None:
            continue
        name = ida_name.get_name(ea)
        mine = ida_bytes.has_user_name(ida_bytes.get_flags(ea)) and name != applied.hashstr(f"{ea:x}")
        comment = user_comment(ea).replace("\t", " ").replace("\n", " / ")
        if mine or comment:
            rows.append(f"{ident}\t{ea - base:#x}\t{name if mine else ''}\t{comment}")
    with open(OUT, "w") as handle:
        handle.write("id\trva\tname\tcomment\n" + "\n".join(rows) + "\n")
    print(f"[re] exported {len(rows)} notes to {OUT}; now run scripts/re/build_re_data.py")


main()
