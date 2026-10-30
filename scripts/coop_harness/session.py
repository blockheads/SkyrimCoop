"""A test session owns its games, connections, fixtures and failure context."""
import contextlib
from .errors import TestFailure
from .game import Game
from .registry import Registry
from .registrations import register_flows
from .runtime import Runtime
from .commands import Commands
from .actions import Actions
from .reporting import Reporting
from .fixtures import Fixtures
from .runner import Runner
from .observations import Observations


class Session(Runtime, Commands, Actions, Reporting, Fixtures, Runner, Observations):
    def __init__(self):
        self.host = Game("")
        self.guest = Game("client")
        self.games = (self.host, self.guest)
        self.HOLD = 0.0
        self.VISIBLE = False
        self.FAILING = False
        self.STATUS_AT_FAILURE = False
        self.CURRENT_STEP = {"description": None, "time": 0.0}
        self.SYMBOLIZE = {}
        self.KNOWN_CRASHES = {game.name: set(self.crash_logs(game.name)) for game in self.games}
        self._next_crash_check = 0.0
        self.registry = Registry()
        register_flows(self.registry)
        self.phase = None
        self.observe = lambda event, **data: None
        self.cleanup_errors = []

    def game(self, value=None):
        if isinstance(value, Game):
            if value not in self.games:
                raise ValueError("game belongs to a different session")
            return value
        if value in (None, "", "host"):
            return self.host
        if value in ("client", "guest"):
            return self.guest
        raise ValueError(f"unknown game {value!r}")

    def failure(self, *args):
        error = TestFailure(*args)
        self.FAILING = True
        error.phase = self.phase
        error.checkpoint = self.CURRENT_STEP["description"]
        if self.STATUS_AT_FAILURE:
            for game in self.games:
                with contextlib.suppress(Exception):
                    error.status[game.name] = game.read_status()
        return error

    def close(self):
        for game in self.games:
            game.close()
