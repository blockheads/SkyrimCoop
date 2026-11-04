"""Per-game paths, display and persistent RPC connections."""
import contextlib
import json
import os
import socket
import time
from pathlib import Path
from .common import (
    GAME,
    TEST_HOME,
    WINDOWS,
)
from .errors import TestFailure

FREE_ENV = "COOP_HARNESS_FREE"  # set: drive the games without holding the duo (a deliberate bypass)
# Harness commands that only read or trace: any process may send them (the diagnostics: look, desync, world-diff).
READ_COMMANDS = frozenset("""act_state actor_pools anim_trace body_trace camera change_log combat_targets container_items counters
    dialogue dialogue_trace engine_events form form_known graph_state graph_writers hud_messages idle_conditions
    info_conditions info_responses info_said input_state is_current_package item_count item_stacks judged_line log_trail
    npc_templates objects papyrus_result player_probe_read quest_fragments ray records ref_enabled reference_effects
    screen script_stacks script_state stack_extras status story_trace surroundings trainings ui_element world_clock
    world_state""".split())
FRONTS = set()  # the sanctioned ways in running in this process (coop_test.py's CLI)
_held = {"until": 0.0}


def check_duo_held(request):
    """Refuses a command that drives the games unless this process holds the duo or came in by coop_test.py
    (docs/design/repro-mcp.md, Robustness): a bare Session in a scratch script goes through the MCP instead."""
    if WINDOWS or FRONTS or os.environ.get(FREE_ENV) or request.get("cmd") in READ_COMMANDS or time.monotonic() < _held["until"]:
        return
    from .actions.repro import duo_home
    from .duo_lock import holds_duo
    if not holds_duo(duo_home()):
        raise TestFailure(f"{request.get('cmd')}: this process holds no duo. Drive the games through the skyrim-coop MCP "
                          f"(extend it if it lacks something), coop_test.py, or `scripts/duo.py run -- <command>`; "
                          f"{FREE_ENV}=1 bypasses on purpose")
    _held["until"] = time.monotonic() + 30


def known_folder(guid):
    """A Windows known folder's path (SHGetKnownFolderPath: Documents may be moved, to OneDrive say)."""
    import ctypes
    import uuid
    from ctypes import wintypes
    folder_id = (ctypes.c_byte * 16).from_buffer_copy(uuid.UUID(guid).bytes_le)
    path = ctypes.c_wchar_p()
    ctypes.windll.shell32.SHGetKnownFolderPath(folder_id, wintypes.DWORD(0), None, ctypes.byref(path))
    try:
        return Path(path.value)
    finally:
        ctypes.windll.ole32.CoTaskMemFree(path)


DOCUMENTS = "FDD39AD0-238F-46AF-ADB4-6C85480369C7"
LOCAL_APP_DATA = "F1B32785-6FBA-4FCF-9D55-7B8E7F157091"


class Game:
    def __init__(self, name, *, install=GAME, prefix=None, display=None):
        self.name = name
        self.install = install
        self.prefix = prefix or TEST_HOME / f"prefix-{name or 'host'}"
        self.control = install / "Data/SKSE/Plugins" / ("SkyrimCoopHotReload" + (f"-{name}" if name else ""))
        if WINDOWS:  # the user's own, shared by both games: Skyrim takes it from the Documents known folder
            self.my_games = known_folder(DOCUMENTS) / "My Games/Skyrim Special Edition"
        else:
            self.my_games = self.prefix / "pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition"
        base = int(os.environ.get("COOP_DISPLAY", ":99").lstrip(":"))
        self.display = display or f":{base + bool(name)}"
        self.tracy_port = 8086 + bool(name)  # TRACY_PORT: where `profile` captures it (Diagnostics/Profile.h)
        self.rpc_connections = {}
        self.held_buttons = set()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        for _, sock, stream in self.rpc_connections.values():
            with contextlib.suppress(OSError):
                stream.close()
            with contextlib.suppress(OSError):
                sock.close()
        self.rpc_connections.clear()

    def rpc(self, request, timeout=3.0):
        """Sends one request to the game; returns the decoded response, or None."""
        check_duo_held(request)
        key = str(self.control)
        try:
            info = json.loads((self.control / "rpc.json").read_text())
        except (OSError, ValueError):
            return None
        connection = self.rpc_connections.get(key)
        if connection and connection[0] != (info.get("pid"), info.get("port")):
            with contextlib.suppress(OSError):
                connection[2].close()
                connection[1].close()
            connection = None
        try:
            if not connection:
                sock = socket.create_connection((info.get("address", "127.0.0.1"), info["port"]), timeout=1.0)
                connection = self.rpc_connections[key] = [(info.get("pid"), info.get("port")), sock, sock.makefile("rb")]
            connection[1].settimeout(timeout)
            connection[1].sendall((json.dumps(request) + "\n").encode())
            line = connection[2].readline()
            if not line:
                raise OSError("closed")
            return json.loads(line)
        except (OSError, ValueError, KeyError):
            if connection:
                with contextlib.suppress(OSError):
                    connection[2].close()
                    connection[1].close()
            self.rpc_connections.pop(key, None)
            return None

    def read_status(self):
        response = self.rpc({"cmd": "status"})
        if response and response.get("ok"):
            return response["status"]
        try:
            status = json.loads((self.control / "status.json").read_text())
        except (OSError, ValueError):
            return None
        heartbeat = status.get("heartbeat_ms", 0) if isinstance(status, dict) else 0
        if not isinstance(heartbeat, (int, float)) or abs(time.time() * 1000 - heartbeat) > 3000:
            return None  # stale file from an earlier game process
        return status
