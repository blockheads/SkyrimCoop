#!/usr/bin/env python3
"""Merge known engine references into a runtime-specific dataset.

Sources, in priority order for names:
  1. docs/re/ida_notes.tsv            names/comments exported from our IDA database (ida_export.py)
  2. reference/CommonLibSSE(-NG)      community RE headers: functions, singletons, vtables, RTTI
                                      (powerof3's fork first, alandtse's NG fills the gaps)
  3. SKSE source (game dir src/)      SKSE's own engine bindings for this exact exe version
  4. meh321 AddressLibraryDatabase    community names for Address Library IDs
plus how *our* code uses each address (POINTER_SKYRIMSE / HOOK_ID: hook / patch / call / data), and which
SKSE plugins in reference/plugins hook or call it (names from their offsets only after meh321's).

Outputs:
  build/re/annotations.json   consumed by scripts/re/ida_apply.py
  docs/re/ENGINE_REFERENCE.md every engine address SkyrimCoop touches, cross-referenced

Run scripts/re/fetch_references.sh first. Overrides: SKYRIM_GAME_DIR.
"""
import json
import hashlib
import sys
import os
from pathlib import Path
import re
import struct
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from skyrim_runtime import runtime_version, library_name, pe_version
from address_library import load as load_address_library

GAME = Path(os.environ.get("SKYRIM_GAME_DIR",
                           "/media/bighass/1f640250-798d-4f2f-87f2-c442109fc5d0/SteamLibrary/steamapps/common/Skyrim Special Edition"))
PLUGINS = ROOT / "reference/plugins"
MEH_NAMES = ROOT / "reference/AddressLibraryDatabase/skyrimae.rename"
CLIENT = ROOT / "Code/client"
IMAGE_BASE = 0x140000000


def configure_runtime(value=None):
    global EXE_VERSION, VERSIONLIB, SKSE_SRC, COMMONLIBS, IDA_NOTES, TARGET_NOTES, OUT_JSON, OUT_MD, EXE
    EXE_VERSION = runtime_version(value)
    short = EXE_VERSION.removesuffix(".0")
    local_library = ROOT / "build/runtime-vendor" / short / "Data/SKSE/Plugins" / library_name(EXE_VERSION)
    VERSIONLIB = Path(os.environ.get("COOP_RE_LIBRARY", local_library if local_library.exists() else GAME / "Data/SKSE/Plugins" / library_name(EXE_VERSION)))
    vendor = ROOT / "build/runtime-vendor" / short
    SKSE_SRC = Path(os.environ.get("COOP_RE_SKSE_SOURCE", vendor / "src" if vendor.exists() else GAME / "src/skse64"))
    ng = Path(os.environ.get("COOP_RE_COMMONLIB", ROOT / "reference/CommonLibSSE-NG"))
    COMMONLIBS = (ROOT / "reference/CommonLibSSE", ng) if EXE_VERSION == "1.6.1170.0" else (ng,)
    IDA_NOTES = ROOT / "docs/re/ida_notes.tsv"
    TARGET_NOTES = IDA_NOTES if EXE_VERSION == "1.6.1170.0" else ROOT / f"docs/re/ida_notes_{short}.tsv"
    OUT_JSON = ROOT / "build/re" / short / "annotations.json"
    OUT_MD = ROOT / "docs/re" / ("ENGINE_REFERENCE.md" if EXE_VERSION == "1.7.104.0" else f"ENGINE_REFERENCE_{short}.md")
    EXE = Path(os.environ.get("COOP_RE_EXE", ROOT / "build/re" / f"SkyrimSE_{short}_unpacked.exe"))


configure_runtime()


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
    """{ae_id: (name, kind, 'path:line')} from every CommonLib tree; the first tree that names an ID wins."""
    found = {}
    for root in COMMONLIBS:
        for ident, entry in load_commonlib_tree(root).items():
            if ident not in found or (not found[ident][0] and entry[0]):
                found[ident] = entry
    return found


def load_commonlib_tree(COMMONLIB):
    """{ae_id: (name, kind, 'path:line')} from RELOCATION_ID(se, ae) uses and the AE offset tables."""
    found = {}
    if not COMMONLIB.exists():
        return found
    relocation = re.compile(r"(?:RELOCATION_ID|REL::RelocationID|REL::VariantID)\(\s*(\d+)\s*,\s*(\d+)(?:\s*,\s*[^)]+)?\s*\)")
    decltype_name = re.compile(r"decltype\(&\s*(?:RE::)?([\w:]+)\s*\)")
    variable = re.compile(r"REL::Relocation<[^>]*>+\s+(\w+)\s*[{(]")
    table_entry = re.compile(r"(?:inline )?constexpr .*?\s(\w+)\s*[{(]\s*(.*?)\s*[})];")
    for path in sorted((COMMONLIB / "include").rglob("*.h")) + sorted((COMMONLIB / "src").rglob("*.cpp")):
        relative = path.relative_to(COMMONLIB.parent)
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
            if (has_ae_block and in_ae) or (not has_ae_block and path.name.startswith("Offsets")):
                table = table_entry.search(line)
                if table:
                    label, body = table.groups()
                    ids = [int(x) for x in re.findall(r"REL::(?:VariantID|RelocationID)\(\s*\d+\s*,\s*(\d+)", body)] or \
                          [int(x) for x in re.findall(r"REL::ID\((\d+)\)", body)] or \
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


def load_plugins():
    """{ae_id: [(name or '', 'plugin/path:line')]} from RELOCATION_ID(se, ae) / VariantID(se, ae, vr) in
    the SKSE plugins under reference/plugins. A name only when the relocation is the function itself
    (no call-site offset, not a generic variable such as `target` or `hook`)."""
    found = defaultdict(list)
    if not PLUGINS.exists():
        return found
    relocation = re.compile(r"(?:RELOCATION_ID|RelocationID|VariantID)\(\s*\d+\s*,\s*(\d+)")
    variable = re.compile(r"(?:REL::Relocation<[^>]*>+|void\*|auto)\s+(?:g_)?(\w+)\s*(?:[{(]|=)")
    generic = re.compile(r"(?i)^(target|hook\w*|func\w*|addr\w*|ptr|call\w*|jmp|vtbl\w*|t?\d*|offset\w*|patch\w*|"
                         r"loc\w*|start|end|base|reloc\w*|\w*hook\d*|[0-9A-Fa-f]+|\w{1,4})$")
    for path in sorted(PLUGINS.rglob("*")):
        if path.suffix not in (".h", ".hpp", ".cpp", ".inl") or not path.is_file() or "extern" in path.parts:
            continue
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            for match in relocation.finditer(line):
                var = variable.search(line)
                named = var and not generic.match(var.group(1)) and not re.search(r"OFFSET|VAR_NUM|\+\s*0x", line)
                found[int(match.group(1))].append((var.group(1) if named else "",
                                                   f"{path.relative_to(PLUGINS)}:{number}"))
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
    paths = [IDA_NOTES] if TARGET_NOTES == IDA_NOTES else [IDA_NOTES, TARGET_NOTES]
    for path in paths:
        if not path.exists():
            continue
        for line in path.read_text().splitlines()[1:]:
            parts = line.split("\t")
            if len(parts) >= 3 and parts[0].isdigit():
                comment = parts[3] if len(parts) > 3 else ""
                if path == IDA_NOTES and EXE_VERSION != "1.6.1170.0" and comment:
                    comment = "From 1.6.1170 (instruction offsets require review): " + comment
                notes[int(parts[0])] = (parts[2], comment)
    return notes


def scan_our_code():
    """Every POINTER_SKYRIMSE, Hooks::Hook, HOOK_ID and Hooks::Resolve in the client, classified by address use."""
    pointer = re.compile(r"POINTER_SKYRIMSE\(\s*([^,]+?)\s*,\s*(\w+)\s*,\s*(\d+)\s*\)")
    hook_call = re.compile(r'\bHooks::Hook<\s*(\d+)\s*>\(\s*&\s*(\w+)\s*,\s*&\s*(\w+)\s*,\s*"[^"]*"'
                           r'(?:\s*,\s*(?:true|false))?\s*\)')
    hook_macro = re.compile(r"\bHOOK_ID(?:_IMMEDIATE)?\(\s*(\d+)\s*,\s*&\s*(\w+)\s*,\s*&?(\w+)\s*\)")
    resolve = re.compile(r"\bHooks::Resolve<\s*(\d+)\s*>\(\s*&\s*(\w+)\s*\)")
    uses = defaultdict(list)
    for path in sorted(CLIENT.rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        text = path.read_text(errors="replace")
        lines = text.splitlines()
        branches = []
        for number, line in enumerate(lines, 1):
            directive = line.strip()
            if re.match(r"#\s*if(?:def|ndef)?\b", directive):
                branches.append(EXE_VERSION == "1.6.1170.0" if directive == "#if COOP_SKYRIM_1_6_1170" else None)
            elif directive == "#else" and branches and branches[-1] is not None:
                branches[-1] = not branches[-1]
            elif directive == "#endif" and branches:
                branches.pop()
            if False in branches or line.lstrip().startswith("//"):
                continue
            direct = re.search(r"\bVersionDbPtr<(.+?)>\s+(.+)", line)
            extras = []
            if direct:
                extras += [(int(m[2]), m[1], direct[1], "data") for m in
                           re.finditer(r"([\w:]+)\s*[({]\s*(\d+)\s*[)}]", direct[2])]
            extras += [(int(m[2]), m[1], "RTTI", "rtti") for m in
                       re.finditer(r"internal::RttiLocator<[^>]+>\s+(\w+)\(\s*(\d+)\s*\)", line)]
            for ident, variable, type_name, kind in extras:
                if kind != "rtti" and re.search(rf"Hooks::(?:Nop|Put\w*|SwapCall)\([^;]*\b{re.escape(variable)}\b", text):
                    kind = "patch"
                uses[ident].append({"file": str(path.relative_to(ROOT)), "line": number, "var": variable,
                                    "type": type_name, "kind": kind, "hook": None, "flags": []})
            for match in pointer.finditer(line):
                type_name, var, ident = match.group(1), match.group(2), int(match.group(3))
                real = re.findall(rf"(\w+)\s*=\s*{var}\.Get\(\)", text)
                hook = next((m.group(1) for r in real for m in
                             re.finditer(rf"\bHOOK(?:_IMMEDIATE)?\(\s*&\s*{r}\s*,\s*&?(\w+)", text)), None)
                if hook:
                    kind = "hook"
                elif re.search(rf"\b(?:Put\w*|Nop|vp::Put\w*|JitAssembly\w*)\s*\([^;]*\b{var}\b", text):
                    kind = "patch"
                elif re.search(rf"\b{var}\.Get\(\)\s*\(|\b{var}\s*\(", text) or \
                        any(re.search(rf"\b{r}\s*\(", text) for r in real):
                    kind = "call"
                else:
                    kind = "data"
                window = " ".join(lines[max(0, number - 3):number + 1])
                flags = ["todo-in-code"] if re.search(r"TODO|not sure|FIXME|\?\?\?", window) else []
                uses[ident].append({"file": str(path.relative_to(ROOT)), "line": number, "var": var,
                                    "type": type_name, "kind": kind, "hook": hook, "flags": flags})
            for match in (*hook_call.finditer(line), *hook_macro.finditer(line)):
                ident, real, hook = int(match.group(1)), match.group(2), match.group(3)
                declaration = re.search(rf"\b(\w+)\s*\*\s*{real}\s*(?:=|;)", text)
                type_name = declaration.group(1) if declaration else f"decltype(*{real})"
                window = " ".join(lines[max(0, number - 3):number + 1])
                flags = ["todo-in-code"] if re.search(r"TODO|not sure|FIXME|\?\?\?", window) else []
                uses[ident].append({"file": str(path.relative_to(ROOT)), "line": number, "var": real,
                                    "type": type_name, "kind": "hook", "hook": hook, "flags": flags})
            # Resolved now, hooked by a HOOK(&Real, ...) later (or only called).
            for match in resolve.finditer(line):
                ident, real = int(match.group(1)), match.group(2)
                hooked = re.search(rf"\bHOOK(?:_IMMEDIATE)?\(\s*&\s*{real}\s*,\s*&?(\w+)", text)
                declaration = re.search(rf"\b(\w+)\s*\*\s*{real}\s*(?:=|;)", text)
                window = " ".join(lines[max(0, number - 3):number + 1])
                flags = ["todo-in-code"] if re.search(r"TODO|not sure|FIXME|\?\?\?", window) else []
                uses[ident].append({"file": str(path.relative_to(ROOT)), "line": number, "var": real,
                                    "type": declaration.group(1) if declaration else f"decltype(*{real})",
                                    "kind": "hook" if hooked else "call", "hook": hooked.group(1) if hooked else None,
                                    "flags": flags})
    return uses


def main():
    if pe_version(EXE) != EXE_VERSION:
        raise SystemExit(f"Executable version does not match {EXE_VERSION}: {EXE}")
    if not VERSIONLIB.exists():
        raise SystemExit(f"Address Library not found: {VERSIONLIB}")
    ids = load_address_library(VERSIONLIB, EXE_VERSION)
    rva_to_id = {rva: ident for ident, rva in ids.items()}
    meh, commonlib, skse, notes, ours, plugins = (load_meh_names(), load_commonlib(), load_skse(rva_to_id),
                                                  load_ida_notes(), scan_our_code(), load_plugins())

    annotations = []
    everything = set(meh) | set(commonlib) | set(skse) | set(notes) | set(ours) | set(plugins)
    for ident in sorted(everything):
        if ident not in ids:
            continue
        cl, sk, ida = commonlib.get(ident), skse.get(ident), notes.get(ident)
        uses = ours.get(ident, [])
        plugin_name = next((n for n, _ in plugins.get(ident, []) if n), None)
        name = (ida and ida[0]) or (cl and cl[0]) or (sk and sk[0]) or \
               (plugin_name if not meh.get(ident) or re.search(r"(sub|Func\d+|unk\w*)_\d+$", meh[ident]) else None) or \
               meh.get(ident) or ("coop_" + uses[0]["var"].removeprefix("s_") if uses else None)
        kind = (cl and cl[1]) or ("func" if uses and uses[0]["kind"] in ("hook", "call") else "data")
        comment = [f"AddressLib ID {ident}"]
        if meh.get(ident) and meh[ident] != name:
            comment.append(f"meh321: {meh[ident]}")
        if cl:
            comment.append(f"CommonLibSSE: {cl[0] or 'function'} ({cl[2]})")
        if sk:
            comment.append(f"SKSE: {sk[0]} ({sk[1]})")
        for plugin_use, where in plugins.get(ident, [])[:4]:
            comment.append(f"Used by plugin: {where}" + (f" as {plugin_use}" if plugin_use else ""))
        for use in uses:
            action = f"hooked by {use['hook']}" if use["kind"] == "hook" else use["kind"]
            comment.append(f"SkyrimCoop: {action} as {use['var']} ({use['file']}:{use['line']})")
        if ida and ida[1]:
            comment.append(ida[1])
        annotations.append({"id": ident, "rva": ids[ident], "name": name, "kind": kind, "comment": "\n".join(comment),
                            "ours": bool(uses), "hook": any(u["kind"] == "hook" for u in uses)})

    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    OUT_JSON.write_text(json.dumps({"image_base": IMAGE_BASE, "exe_version": EXE_VERSION,
                                    "input_sha256": hashlib.sha256(EXE.read_bytes()).hexdigest(),
                                    "annotations": annotations}))
    write_markdown(ids, meh, commonlib, skse, ours, notes, plugins)
    used = [a for a in annotations if a["ours"]]
    print(f"{len(annotations)} annotated addresses ({len(commonlib)} CommonLib, {len(skse)} SKSE, "
          f"{len(meh)} meh321, {len(plugins)} used by plugins, {len(notes)} IDA notes); SkyrimCoop uses {len(used)} "
          f"({sum(a['hook'] for a in used)} hooked)")
    print(f"Wrote {OUT_JSON.relative_to(ROOT)} and {OUT_MD.relative_to(ROOT)}")


def write_markdown(ids, meh, commonlib, skse, ours, notes, plugins):
    def link(use):
        return f"[{Path(use['file']).name}:{use['line']}](../../{use['file']}#L{use['line']})"

    def row(ident, use):
        cl, sk, ida = commonlib.get(ident), skse.get(ident), notes.get(ident)
        sources = " ".join(s for s, ok in (("IDA", ida), ("CL", cl), ("SKSE", sk), ("PL", ident in plugins),
                                            ("meh", ident in meh)) if ok)
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
        f"Every SkyrimSE.exe {EXE_VERSION} address SkyrimCoop uses, cross-referenced with the community references.",
        "RVAs are relative to image base `0x140000000`. Sources: **CL** = CommonLibSSE (signature/layout",
        "available; powerof3's fork, else alandtse's NG), **SKSE** = SKSE source, **PL** = hooked or called by an",
        "SKSE plugin in `reference/plugins` (see the IDA comment), **meh** = meh321 AddressLibraryDatabase name only,",
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
