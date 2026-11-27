#!/usr/bin/env python3
"""The RE notes this branch added since origin/dev hold up (docs/re/README.md): every IDA notes or journal row names a
real Address Library ID at its own RVA (build_re_data.py drops a bad row silently), and the notes and
docs/re/systems/ name functions rather than citing IDA's auto names (sub_...; journals are history, not checked); and
each Address Library ID is declared in one file of Code/ (twice_declared). Run by scripts/check.sh."""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path[:0] = [str(ROOT / "scripts"), str(ROOT / "scripts/re")]
from address_library import load as load_address_library  # noqa: E402
from skyrim_runtime import library_name  # noqa: E402

AUTO_NAME = re.compile(r"\bsub_[0-9A-Fa-f]{6,}\b")
NOTES = re.compile(r"^docs/re/ida_notes(?:_(?P<version>[\d.]+))?\.tsv$")
JOURNAL = re.compile(r"^docs/re/journal/(?P<version>[\d.]+)/[^/]+\.tsv$")


def version_of(path):
    """The runtime whose Address Library a notes or journal file uses; None for ida_notes.tsv (frozen, from 1.6.1170)."""
    match = NOTES.match(path) or JOURNAL.match(path)
    return match.group("version") if match else None


def row_problem(path, line, library):
    """What is wrong with one added row, or None. `library` is {id: rva}, or None when not installed."""
    if line.startswith(("id\t", "time\t")):
        return None
    parts = line.split("\t")
    if JOURNAL.match(path):
        if len(parts) != 5:
            return f"{len(parts)} columns, not 5"
        ident, rva = parts[2], parts[3]
        if not ident:
            return None  # no ID: a declaration, a data rename, or code the Address Library doesn't list
    else:
        if len(parts) < 3:
            return f"{len(parts)} columns, not 4"
        ident, rva = parts[0], parts[1]
    if not ident.isdigit():
        return f"ID {ident!r} is not a number"
    try:
        offset = int(rva, 16)
    except ValueError:
        return f"RVA {rva!r} is not hex"
    if library is not None and library.get(int(ident)) != offset:
        actual = library.get(int(ident))
        return f"ID {ident} is not in the Address Library" if actual is None else \
            f"ID {ident} is at {actual:#x}, not {offset:#x}"
    return None


def problems(added, libraries):
    """`added` {path: [added lines]}; `libraries` {version: {id: rva} or None}. Returns one line per problem."""
    found = []
    for path, lines in sorted(added.items()):
        version = version_of(path)
        for line in lines:
            if version and (problem := row_problem(path, line, libraries.get(version))):
                found.append(f"{path}: {problem}: {line[:100]}")
            elif (path.startswith("docs/re/systems/") or NOTES.match(path)) and (match := AUTO_NAME.search(line)):
                found.append(f"{path}: {match.group(0)} is IDA's auto name; name the function in IDA and use the "
                             f"name (with its ID): {line.strip()[:100]}")
    return found


def twice_declared(uses):
    """IDs declared in more than one file, as {id: [files]}. `uses` is build_re_data.scan_our_code()'s {id: [use]}.
    A declaration types an address as a function or data: a VersionDbPtr<T> or POINTER_SKYRIMSE(T, ...), and a
    Hooks::Resolve that is only called. Not declarations: T void or uint8_t (a bare address, compared or patched at),
    patches and RTTI, and hooks (HOOK_ID, Hooks::Hook, a Resolve hooked or chained): a hook's signature is the
    hook's, checked with it (CLAUDE.md), and a call of the hooked ID elsewhere is a declaration of its own. A file
    counts once however often it declares the ID. One declaration, in its engine struct's file, is what both call."""
    found = {}
    for ident, each in sorted(uses.items()):
        files = sorted({use["file"] for use in each if use["kind"] not in ("hook", "patch", "rtti")
                        and use["type"] not in ("void", "uint8_t")})
        if len(files) > 1:
            found[ident] = files
    return found


def git(*arguments):
    return subprocess.run(["git", "-C", str(ROOT), *arguments], capture_output=True, text=True).stdout


def added_lines(base):
    """{path: [lines added since base]} for the RE docs, untracked files whole."""
    added, path = {}, None
    for line in git("diff", "-U0", base, "--", "docs/re").splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif path and line.startswith("+"):
            added.setdefault(path, []).append(line[1:])
    for path in git("ls-files", "--others", "--exclude-standard", "--", "docs/re").split():
        added[path] = (ROOT / path).read_text().splitlines()
    return added


def library(version):
    path = ROOT / "build/runtime-vendor" / version / "Data/SKSE/Plugins" / library_name(version)
    return load_address_library(path) if path.exists() else None


def main():
    from build_re_data import scan_our_code
    if twice := twice_declared(scan_our_code()):
        print("Address Library IDs declared in more than one file (declare each once, in its engine struct's file):")
        for ident, files in twice.items():
            print(f"  {ident}: {', '.join(files)}")
        return 1
    base =git("merge-base", "HEAD", "origin/dev").strip()
    if not base:
        print("check_re: no origin/dev to compare with; skipped")
        return 0
    added = added_lines(base)
    versions = {version_of(path) for path in added} - {None}
    libraries = {version: library(version) for version in versions}
    if found := problems(added, libraries):
        print("RE notes added on this branch that don't hold up:")
        for each in found:
            print(f"  {each}")
        return 1
    unchecked = sorted(version for version, ids in libraries.items() if ids is None)
    note = f" (no Address Library for {', '.join(unchecked)}: IDs not checked)" if unchecked else ""
    print(f"check_re: {sum(map(len, added.values()))} added RE line(s) hold up{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
