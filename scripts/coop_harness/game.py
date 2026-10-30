"""Per-game paths, display and persistent RPC connections."""
import contextlib
import json
import os
import socket
import time
from .common import (
    GAME,
    TEST_HOME,
)

class Game:
    def __init__(self, name, *, install=GAME, prefix=None, display=None):
        self.name = name
        self.install = install
        self.prefix = prefix or TEST_HOME / f"prefix-{name or 'host'}"
        self.control = install / "Data/SKSE/Plugins" / ("SkyrimCoopHotReload" + (f"-{name}" if name else ""))
        self.my_games = self.prefix / "pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition"
        base = int(os.environ.get("COOP_DISPLAY", ":99").lstrip(":"))
        self.display = display or f":{base + bool(name)}"
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
