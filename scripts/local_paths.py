"""This machine's Steam, game and tool paths: an environment variable, else ~/.config/skyrimcoop/paths.env
(KEY=value lines, outside the repo), else the default. `python3 scripts/local_paths.py NAME` prints one."""
import os
from pathlib import Path
import sys

FILE = Path.home() / ".config/skyrimcoop/paths.env"
WINDOWS = sys.platform == "win32"


def _read_file():
    values = {}
    if FILE.is_file():
        for line in FILE.read_text().splitlines():
            key, sep, value = line.partition("=")
            if sep and not key.strip().startswith("#"):
                values[key.strip()] = value.strip().strip('"')
    return values


_FILE_VALUES = _read_file()


def setting(name, default=None):
    """The variable if set (even empty), else the file's value, else the default."""
    if name in os.environ:
        return os.environ[name]
    return _FILE_VALUES.get(name, default)


STEAM_LIBRARY = Path(setting("SKYRIM_STEAM_LIBRARY", r"C:\Program Files (x86)\Steam\steamapps\common" if WINDOWS
                             else Path.home() / ".local/share/Steam/steamapps/common"))
# The Steam install's game folder, read only.
STEAM_GAME = Path(setting("SKYRIM_STEAM_GAME_DIR", STEAM_LIBRARY / "Skyrim Special Edition"))

DEFAULTS = {"SKYRIM_STEAM_LIBRARY": STEAM_LIBRARY, "SKYRIM_STEAM_GAME_DIR": STEAM_GAME}

if __name__ == "__main__":
    name = sys.argv[1]
    value = setting(name, DEFAULTS.get(name))
    if value is None:
        sys.exit(f"{name} is not set: export it or add {name}=<path> to {FILE}")
    print(value)
