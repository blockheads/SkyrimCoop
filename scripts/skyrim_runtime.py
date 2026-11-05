"""Supported Steam runtimes shared by staging, packages and RE tools."""
import os
from pathlib import Path
import struct
import re

LATEST = "1.7.104.0"
SUPPORTED = (LATEST, "1.6.1170.0")


def runtime_version(value=None):
    value = value or os.environ.get("COOP_SKYRIM_VERSION", "latest")
    if value == "latest":
        return LATEST
    value = value if value.count(".") == 3 else value + ".0"
    if value not in SUPPORTED:
        raise ValueError(f"unsupported Skyrim runtime {value}; choose latest or 1.6.1170")
    return value


def library_name(version=None):
    return f"versionlib-{runtime_version(version).replace('.', '-')}.bin"


def skse_dll(version=None):
    return "skse64_" + runtime_version(version).removesuffix(".0").replace(".", "_") + ".dll"


def database_executable(recorded, database):
    """The executable an IDA database was made from: where IDA recorded it, else COOP_RE_EXE, else beside the database
    (the recorded path dies with the worktree that made it)."""
    if os.path.exists(recorded):
        return recorded
    return os.environ.get("COOP_RE_EXE") or os.path.join(os.path.dirname(database), os.path.basename(recorded))


def pe_version(path):
    data = Path(path).read_bytes()
    start = data.find(struct.pack("<II", 0xFEEF04BD, 0x10000))
    if start < 0:
        raise ValueError(f"{path}: no PE version resource")
    major_minor, revision_build = struct.unpack_from("<II", data, start + 8)
    return ".".join(map(str, (major_minor >> 16, major_minor & 65535, revision_build >> 16, revision_build & 65535)))


def game_executable(folder, version=None):
    version = runtime_version(version)
    candidates = [Path(os.environ["SKYRIM_EXECUTABLE"])] if os.environ.get("SKYRIM_EXECUTABLE") else [
        Path(folder) / "SkyrimSE.exe", Path(folder) / f"SkyrimSE_{version.removesuffix('.0')}.exe"]
    for path in candidates:
        if path.is_file() and pe_version(path) == version:
            return path
    raise ValueError(f"No Skyrim {version} executable in {folder}; set SKYRIM_EXECUTABLE to the matching copy")


def runtime_from_log(text):
    match = re.search(r"Skyrim(?:SE)?\s*(?:v(?:ersion)?\s*)?(1\.[67]\.\d+(?:\.\d+)?)", text)
    return runtime_version(match[1]) if match else runtime_version()
