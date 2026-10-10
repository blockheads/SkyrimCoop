#!/usr/bin/env python3
"""Merge everything known about SkyrimSE.exe 1.6.1170 into one dataset.

Sources, in priority order for names:
  1. docs/re/ida_notes.tsv            names/comments exported from our IDA database (ida_export.py)
  2. reference/CommonLibSSE           community RE headers: functions, singletons, vtables, RTTI
  3. SKSE source (game dir src/)      SKSE's own engine bindings for this exact exe version
  4. meh321 AddressLibraryDatabase    community names for Address Library IDs
plus how *our* code uses each address (POINTER_SKYRIMSE: hook / patch / call / data).

Outputs:
  build/re/annotations.json   consumed by scripts/re/ida_apply.py
  docs/re/ENGINE_REFERENCE.md every engine address SkyrimCoop touches, cross-referenced

Run scripts/re/fetch_references.sh first. Overrides: SKYRIM_GAME_DIR.
"""
import json
import os
from pathlib import Path
import re
import struct
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
GAME = Path(os.environ.get("SKYRIM_GAME_DIR",
                           "/media/bighass/BlackHole/SteamLibrary/steamapps/common/Skyrim Special Edition"))
VERSIONLIB = GAME / "Data/SKSE/Plugins/versionlib-1-6-1170-0.bin"
SKSE_SRC = GAME / "src/skse64"
COMMONLIB = ROOT / "reference/CommonLibSSE"
MEH_NAMES = ROOT / "reference/AddressLibraryDatabase/skyrimae.rename"
IDA_NOTES = ROOT / "docs/re/ida_notes.tsv"
CLIENT = ROOT / "Code/client"
OUT_JSON = ROOT / "build/re/annotations.json"
OUT_MD = ROOT / "docs/re/ENGINE_REFERENCE.md"
IMAGE_BASE = 0x140000000


def load_address_library(path):
    """Address Library v2 format (same decoder as Code/client/VersionDb.h). Returns {id: rva}."""
    data = path.read_bytes()
    pos = 0

    def read(fmt):
        nonlocal pos
        value = struct.unpack_from("<" + fmt, data, pos)[0]
        pos += struct.calcsize("<" + fmt)
        return value

    if read("i") != 2:
        raise SystemExit(f"{path}: unsupported Address Library format")
    for _ in range(4):
        read("i")
    name_length = read("i")
    pos += name_length  # module name
    pointer_size, count = read("i"), read("i")
    previous_id = previous_offset = 0
    ids = {}
    for _ in range(count):
        kind = read("B")
        low, high = kind & 0xF, kind >> 4
        q1 = {0: lambda: read("Q"), 1: lambda: previous_id + 1, 2: lambda: previous_id + read("B"),
              3: lambda: previous_id - read("B"), 4: lambda: previous_id + read("H"),
              5: lambda: previous_id - read("H"), 6: lambda: read("H"), 7: lambda: read("I")}[low]()
        base = previous_offset // pointer_size if high & 8 else previous_offset
        q2 = {0: lambda: read("Q"), 1: lambda: base + 1, 2: lambda: base + read("B"),
              3: lambda: base - read("B"), 4: lambda: base + read("H"), 5: lambda: base - read("H"),
              6: lambda: read("H"), 7: lambda: read("I")}[high & 7]()
        if high & 8:
            q2 *= pointer_size
        ids[q1] = q2
        previous_id, previous_offset = q1, q2
    return ids


def load_meh_names():
    names = {}
    if not MEH_NAMES.exists():
        return names
    for line in MEH_NAMES.read_text(errors="replace").splitlines():
        parts = line.split(" ", 1)
        if len(parts) == 2 and parts[0].isdigit():
            ident, name = int(parts[0]), parts[1].strip()
            # "_*" means "append something unique"; keep the ID only for placeholder names.
            stem = name[:-2] if name.endswith("_*") else name
            last = stem.split("::")[-1]
            names[ident] = f"{stem}_{ident}" if re.fullmatch(r"(sub|Func\d+|unk\w*)", last) else stem
    return names


def load_commonlib():
    """{ae_id: (name, kind, 'path:line')} from RELOCATION_ID(se, ae) uses and the AE offset tables."""
    found = {}
    if not COMMONLIB.exists():
        return found
    relocation = re.compile(r"(?:RELOCATION_ID|REL::RelocationID)\(\s*(\d+)\s*,\s*(\d+)\s*\)")
    decltype_name = re.compile(r"decltype\(&\s*(?:RE::)?([\w:]+)\s*\)")
    variable = re.compile(r"REL::Relocation<[^>]*>+\s+(\w+)\s*[{(]")
    table_entry = re.compile(r"inline constexpr .*?\s(\w+)\s*\{\s*(.*?)\s*\};")
    for path in sorted((COMMONLIB / "include").rglob("*.h")) + sorted((COMMONLIB / "src").rglob("*.cpp")):
        relative = path.relative_to(COMMONLIB)
        lines = path.read_text(errors="replace").splitlines()
        in_ae, has_ae_block = False, any("#ifdef SKYRIM_SUPPORT_AE" in line for line in lines)
        for number, line in enumerate(lines, 1):
            if line.startswith("#ifdef SKYRIM_SUPPORT_AE"):
                in_ae = True
            elif line.startswith(("#else", "#endif")):
                in_ae = False
            for match in relocation.finditer(line):
                ae_id = int(match.group(2))
                context = "\n".join(lines[max(0, number - 6):number])
                names = decltype_name.findall(context)
                if names:
                    name, kind = names[-1], "func"
                else:
                    var = variable.search(line)
                    # A bare "func" relocation without decltype says nothing; let other sources name it.
                    generic = not var or var.group(1) in ("func", "function")
                    name = "" if generic else f"{path.stem}::{var.group(1)}"
                    kind = "func" if generic else "data"
                found.setdefault(ae_id, (name, kind, f"{relative}:{number}"))
            if has_ae_block and in_ae:
                table = table_entry.search(line)
                if table:
                    label, body = table.groups()
                    ids = [int(x) for x in re.findall(r"REL::ID\((\d+)\)", body)] or \
                          [int(x) for x in re.findall(r"^(\d+)$", body)]
                    for index, ident in enumerate(ids):
                        if label.startswith("VTABLE_"):
                            name, kind = "vtbl_" + label[7:] + (f"_{index}" if index else ""), "vtbl"
                        elif label.startswith("RTTI_"):
                            name, kind = "rtti_" + label[5:], "rtti"
                        else:
                            name, kind = label, "data"
                        found.setdefault(ident, (name, kind, f"{relative}:{number}"))
    return found


def load_skse(rva_to_id):
    """{id: (name, 'file:line')} from SKSE's RelocAddr/RelocPtr/DEFINE_MEMBER_FN offsets."""
    found = {}
    if not SKSE_SRC.exists():
        return found
    reloc = re.compile(r"Reloc(?:Addr|Ptr)\s*<.*?>\s*([A-Za-z_][\w:]*)\s*\(\s*(0x[0-9A-Fa-f]+)")
    member = re.compile(r"DEFINE_MEMBER_FN\w*\(\s*(\w+)\s*,.*?(0x[0-9A-Fa-f]{6,})")
    owner = re.compile(r"^\s*(?:class|struct)\s+(\w+)")
    for path in sorted(SKSE_SRC.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        current = path.stem
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            match = owner.match(line)
            if match and not line.rstrip().endswith(";"):
                current = match.group(1)
            for regex, qualify in ((reloc, False), (member, True)):
                match = regex.search(line)
                if match:
                    ident = rva_to_id.get(int(match.group(2), 16))
                    if ident is not None:
                        name = f"{current}::{match.group(1)}" if qualify else match.group(1)
                        found.setdefault(ident, (name, f"{path.relative_to(SKSE_SRC)}:{number}"))
    return found


def load_ida_notes():
    notes = {}
    if IDA_NOTES.exists():
        for line in IDA_NOTES.read_text().splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) >= 3 and parts[0].isdigit():
                notes[int(parts[0])] = (parts[2], parts[3] if len(parts) > 3 else "")
    return notes


def scan_our_code():
    """Every POINTER_SKYRIMSE in the client, classified by how the address is used."""
    pointer = re.compile(r"POINTER_SKYRIMSE\(\s*([^,]+?)\s*,\s*(\w+)\s*,\s*(\d+)\s*\)")
    uses = defaultdict(list)
    for path in sorted(CLIENT.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        text = path.read_text(errors="replace")
        lines = text.splitlines()
        for number, line in enumerate(lines, 1):
            for match in pointer.finditer(line):
                type_name, var, ident = match.group(1), match.group(2), int(match.group(3))
                real = re.findall(rf"(\w+)\s*=\s*{var}\.Get\(\)", text)
                hook = next((m.group(1) for r in real for m in
                             re.finditer(rf"TP_HOOK(?:_IMMEDIATE)?\(\s*&\s*{r}\s*,\s*&?(\w+)", text)), None)
                if hook:
                    kind = "hook"
                elif re.search(rf"\b(?:Put\w*|Nop|vp::Put\w*|JitAssembly\w*)\s*\([^;]*\b{var}\b", text):
                    kind = "patch"
                elif re.search(rf"ThisCall\(\s*{var}\b|\b{var}\.Get\(\)\s*\(|\b{var}\s*\(", text) or \
                        any(re.search(rf"ThisCall\(\s*{r}\b|\b{r}\s*\(", text) for r in real):
                    kind = "call"
                else:
                    kind = "data"
                window = " ".join(lines[max(0, number - 3):number + 1])
                flags = ["todo-in-code"] if re.search(r"TODO|not sure|FIXME|\?\?\?", window) else []
                uses[ident].append({"file": str(path.relative_to(ROOT)), "line": number, "var": var,
                                    "type": type_name, "kind": kind, "hook": hook, "flags": flags})
    return uses


def main():
    if not VERSIONLIB.exists():
        raise SystemExit(f"Address Library not found: {VERSIONLIB}")
    ids = load_address_library(VERSIONLIB)
    rva_to_id = {rva: ident for ident, rva in ids.items()}
    meh, commonlib, skse, notes, ours = (load_meh_names(), load_commonlib(), load_skse(rva_to_id),
                                         load_ida_notes(), scan_our_code())

    annotations = []
    everything = set(meh) | set(commonlib) | set(skse) | set(notes) | set(ours)
    for ident in sorted(everything):
        if ident not in ids:
            continue
        cl, sk, ida = commonlib.get(ident), skse.get(ident), notes.get(ident)
        uses = ours.get(ident, [])
        name = (ida and ida[0]) or (cl and cl[0]) or (sk and sk[0]) or meh.get(ident) or \
               ("coop_" + uses[0]["var"].removeprefix("s_") if uses else None)
        kind = (cl and cl[1]) or ("func" if uses and uses[0]["kind"] in ("hook", "call") else "data")
        comment = [f"AddressLib ID {ident}"]
        if meh.get(ident) and meh[ident] != name:
            comment.append(f"meh321: {meh[ident]}")
        if cl:
            comment.append(f"CommonLibSSE: {cl[0] or 'function'} ({cl[2]})")
        if sk:
            comment.append(f"SKSE: {sk[0]} ({sk[1]})")
        for use in uses:
            action = f"hooked by {use['hook']}" if use["kind"] == "hook" else use["kind"]
            comment.append(f"SkyrimCoop: {action} as {use['var']} ({use['file']}:{use['line']})")
        if ida and ida[1]:
            comment.append(ida[1])
        annotations.append({"id": ident, "rva": ids[ident], "name": name, "kind": kind, "comment": "\n".join(comment),
                            "ours": bool(uses), "hook": any(u["kind"] == "hook" for u in uses)})

    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps({"image_base": IMAGE_BASE, "exe_version": "1.6.1170.0",
                                    "annotations": annotations}))
    write_markdown(ids, meh, commonlib, skse, ours, notes)
    used = [a for a in annotations if a["ours"]]
    print(f"{len(annotations)} annotated addresses ({len(commonlib)} CommonLib, {len(skse)} SKSE, "
          f"{len(meh)} meh321, {len(notes)} IDA notes); SkyrimCoop uses {len(used)} "
          f"({sum(a['hook'] for a in used)} hooked)")
    print(f"Wrote {OUT_JSON.relative_to(ROOT)} and {OUT_MD.relative_to(ROOT)}")


def write_markdown(ids, meh, commonlib, skse, ours, notes):
    def link(use):
        return f"[{Path(use['file']).name}:{use['line']}](../../{use['file']}#L{use['line']})"

    def row(ident, use):
        cl, sk, ida = commonlib.get(ident), skse.get(ident), notes.get(ident)
        sources = " ".join(s for s, ok in (("IDA", ida), ("CL", cl), ("SKSE", sk), ("meh", ident in meh)) if ok)
        flags = list(use["flags"])
        if not (ida or cl or sk or ident in meh):
            flags.append("**undocumented**")
        if ident not in ids:
            flags.append("**ID missing from Address Library**")
        name = (ida and ida[0]) or (cl and cl[0]) or (sk and sk[0]) or meh.get(ident, "?")
        rva = f"`{ids[ident]:#x}`" if ident in ids else "-"
        what = f"hook → `{use['hook']}`" if use["kind"] == "hook" else use["kind"]
        return f"| {ident} | {rva} | `{name}` | {sources or '-'} | {what} | `{use['var']}` | {link(use)} | {', '.join(flags)} |"

    rows_by_area = defaultdict(list)
    hooks, undocumented = [], 0
    for ident, uses in sorted(ours.items()):
        for use in uses:
            area = str(Path(use["file"]).parent.relative_to("Code/client")) or "."
            rows_by_area[area].append(row(ident, use))
            if use["kind"] == "hook":
                hooks.append(row(ident, use))
        if not (ident in notes or ident in commonlib or ident in skse or ident in meh):
            undocumented += 1
    header = "| ID | RVA | Community name | Sources | Use | Our symbol | Where | Flags |\n|---|---|---|---|---|---|---|---|"
    total = len(ours)
    documented = sum(1 for i in ours if i in commonlib or i in skse)
    out = [
        "# Engine reference (generated)",
        "",
        "<!-- Generated by scripts/re/build_re_data.py — do not edit; put notes in docs/re/systems/. -->",
        "",
        "Every SkyrimSE.exe 1.6.1170 address SkyrimCoop uses, cross-referenced with the community references.",
        "RVAs are relative to image base `0x140000000`. Sources: **CL** = CommonLibSSE (signature/layout",
        "available), **SKSE** = SKSE source, **meh** = meh321 AddressLibraryDatabase name only,",
        "**IDA** = our own IDA work (`docs/re/ida_notes.tsv`).",
        "",
        f"- **{total}** distinct engine addresses used; **{documented}** have code in CommonLib or SKSE; "
        f"**{undocumented}** have no community name at all.",
        f"- **{len(hooks)}** hooks (these replace engine code, so check them first when the game crashes).",
        "",
        "## Hooks",
        "",
        header,
        *hooks,
        "",
    ]
    for area in sorted(rows_by_area):
        out += [f"## {area}", "", header, *rows_by_area[area], ""]
    OUT_MD.parent.mkdir(parents=True, exist_ok=True)
    OUT_MD.write_text("\n".join(out))


if __name__ == "__main__":
    main()
