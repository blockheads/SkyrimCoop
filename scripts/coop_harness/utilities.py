"""Utilities for the co-op harness."""
import os
import time
from .common import START


def log(message):
    print(f"[{time.monotonic() - START:6.1f}s] {message}", flush=True)


def atomic_write(path, text):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text)
    os.replace(temporary, path)


def set_ini(path, values):
    """Set [section] key=value pairs, keeping everything else in the file."""
    lines = path.read_text(errors="replace").splitlines() if path.exists() else []
    for section, entries in values.items():
        header = f"[{section}]".lower()
        start = next((i for i, line in enumerate(lines) if line.strip().lower() == header), None)
        if start is None:
            lines += ["", f"[{section}]"]
            start = len(lines) - 1
        end = next((i for i in range(start + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
        for key, value in entries.items():
            found = next((i for i in range(start + 1, end)
                          if lines[i].split("=", 1)[0].strip().lower() == key.lower()), None)
            if found is None:
                lines.insert(end, f"{key}={value}")
                end += 1
            else:
                lines[found] = f"{key}={value}"
    path.write_text("\r\n".join(lines) + "\r\n")
