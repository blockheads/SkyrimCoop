#!/usr/bin/env python3
"""Look up engine addresses for IDA: Address Library ID <-> RVA <-> address, our name and comment for it (IDA notes and
the RE journals over the community names), and where our code hooks or calls it.

  scripts/re/id.py 37507              an Address Library ID
  scripts/re/id.py 0x140689370 0x689370 ...   an address, or an RVA (below the image base)
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path[:0] = [str(HERE), str(HERE.parent)]
import build_re_data  # noqa: E402
import journal  # noqa: E402
from ida_hook import Checkout  # noqa: E402


def community_names(version):
    """{id: (name, comment)} from the last build_re_data run, this checkout's or the main checkout's."""
    main = Checkout(build_re_data.ROOT).database.parent
    for path in (build_re_data.OUT_JSON, main / version / "annotations.json"):
        if path.exists():
            return {entry["id"]: (entry["name"], entry["comment"]) for entry in json.loads(path.read_text())["annotations"]
                    if "id" in entry}
    return {}


def resolve(argument, ids, rvas):
    """(id, rva) for an ID, an address or an RVA; None if the Address Library doesn't know it."""
    text = argument.strip()
    if text.isdigit():
        ident = int(text)
        return (ident, ids[ident]) if ident in ids else None
    value = int(text, 16)
    rva = value - journal.IMAGE_BASE if value >= journal.IMAGE_BASE else value
    return (rvas.get(rva), rva)


def main(arguments):
    if not arguments or arguments[0] in ("-h", "--help"):
        print(__doc__.strip())
        return 0 if arguments else 2
    version = build_re_data.EXE_VERSION.removesuffix(".0")
    ids = build_re_data.load_address_library(build_re_data.VERSIONLIB)
    rvas = {rva: ident for ident, rva in ids.items()}
    ours = build_re_data.load_ida_notes()
    community = community_names(version)
    uses = build_re_data.scan_our_code()
    instance = Checkout(build_re_data.ROOT).shared_instance()
    for argument in arguments:
        try:
            found = resolve(argument, ids, rvas)
        except ValueError:
            found = None
        if not found:
            print(f"{argument}: not an Address Library ID or address in {version}\n")
            continue
        ident, rva = found
        name, comment = ours.get(ident, ("", ""))
        known, generated = community.get(ident, ("", ""))
        print(f"{ident if ident is not None else '(no ID)'}  rva {rva:#x}  address {rva + journal.IMAGE_BASE:#x}")
        print(f"  name     {name or known or '?'}" + (f"   (community: {known})" if name and known and known != name else ""))
        if comment:
            print(f"  ours     {comment}")
        for use in uses.get(ident, []):
            role = f"hook -> {use['hook']}" if use["kind"] == "hook" else use["kind"]
            print(f"  code     {role}: {use['var']} ({use['file']}:{use['line']})")
        where = f"instance_id {instance}" if instance else "no IDA instance has the shared database open"
        print(f"  IDA      decompile addr {rva + journal.IMAGE_BASE:#x} ({where})\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
