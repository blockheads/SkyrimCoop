"""Apply build/re/annotations.json to the open IDA database (SkyrimSE 1.6.1170, unpacked).

In IDA: File > Script file... > scripts/re/ida_apply.py (re-run any time after
build_re_data.py). Headless: scripts/re/make_ida_db.sh.

Names you set yourself are never overwritten: only auto names (sub_/off_/...) and
names this script set earlier are replaced. Our comments sit after a "[re]" marker
line; text you write above it is kept.
"""
import json
import os

import ida_auto
import ida_bytes
import ida_funcs
import ida_kernwin
import ida_nalt
import ida_name
import ida_netnode
import idc

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.environ.get("COOP_RE_ANNOTATIONS", os.path.join(ROOT, "build", "re", "annotations.json"))
MARKER = "[re]"
HOOK_COLOR = 0x9CD0FF  # BGR: orange, engine code SkyrimCoop replaces
OURS_COLOR = 0xC8EBC8  # BGR: green, engine code SkyrimCoop calls or reads


def our_comment(existing, generated):
    keep = (existing or "").split(MARKER)[0].rstrip()
    return (keep + "\n" if keep else "") + MARKER + "\n" + generated


def main():
    with open(DATA) as handle:
        data = json.load(handle)
    base = ida_nalt.get_imagebase()
    if base != data["image_base"]:
        print(f"[re] warning: database image base {base:#x} != {data['image_base']:#x}; using database base")
    applied = ida_netnode.netnode("$ skyrimcoop.re", 0, True)  # ea -> name we set last time
    named = kept = functions = 0
    for entry in data["annotations"]:
        ea = base + entry["rva"]
        if not ida_bytes.is_mapped(ea):
            continue
        if entry["kind"] == "func" and not ida_funcs.get_func(ea) and ida_bytes.is_code(ida_bytes.get_flags(ea)):
            functions += ida_funcs.add_func(ea)
        current = ida_name.get_name(ea)
        ours_before = applied.hashstr(f"{ea:x}")
        user_named = ida_bytes.has_user_name(ida_bytes.get_flags(ea)) and current != ours_before
        if entry["name"] and not user_named:
            if ida_name.set_name(ea, entry["name"], ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE):
                applied.hashset_buf(f"{ea:x}", ida_name.get_name(ea))
                named += 1
        elif user_named:
            kept += 1
        function = ida_funcs.get_func(ea)
        if function and function.start_ea == ea:
            ida_funcs.set_func_cmt(function, our_comment(ida_funcs.get_func_cmt(function, True), entry["comment"]), True)
        else:
            ida_bytes.set_cmt(ea, our_comment(ida_bytes.get_cmt(ea, True), entry["comment"]), True)
        if entry["hook"]:
            idc.set_color(ea, idc.CIC_FUNC if function else idc.CIC_ITEM, HOOK_COLOR)
        elif entry["ours"]:
            idc.set_color(ea, idc.CIC_FUNC if function else idc.CIC_ITEM, OURS_COLOR)
    print(f"[re] {len(data['annotations'])} annotations: {named} names set, {kept} user names kept, "
          f"{functions} functions created")


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
