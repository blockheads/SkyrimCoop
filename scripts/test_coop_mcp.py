#!/usr/bin/env python3
"""The repro MCP's robustness (docs/design/repro-mcp.md, Robustness): reload, rounds, the trace and log reads, the
export of verbs and rounds, the duo refusal for bare sessions, and the scenario-origin check."""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
import unittest.mock
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_scenarios  # noqa: E402
from coop_harness import game as game_module, mcp_tools  # noqa: E402
from coop_harness.actions import repro  # noqa: E402
from coop_harness.duo_lock import Token, holds_duo  # noqa: E402
from coop_harness.errors import TestFailure  # noqa: E402
from coop_harness.objects import trace_lines  # noqa: E402


class FakeSession:
    """The Session as the tools see it: verbs, the recorder, a plugin log."""

    def __init__(self, log_path=None):
        self.calls, self.recorded, self.log_path = [], [], log_path

    def failure(self, text):
        return TestFailure(text)

    def record_call(self, tool, arguments, ok, result=None):
        self.recorded.append(tool)

    def plugin_log(self, name):
        return self.log_path

    def swing(self, instance="", times=1):
        """Swings."""
        self.calls.append((instance, times))
        if len(self.calls) == 3:
            raise TestFailure("the third swing missed")
        return {"crossed": len(self.calls) == 4}


class Server:
    def __init__(self):
        self.tools = {}

    def add_tool(self, function, name):
        self.tools[name] = function


def registered(session):
    state = {"session": session, "duo": {}, "jobs": {}, "trace": None, "logs": {}}
    mcp_tools.register(Server(), state)
    return state


class Tools(unittest.TestCase):
    def test_verb_calls_a_session_method_with_host_or_guest_and_records_once(self):
        session = FakeSession()
        registered(session)
        self.assertEqual(mcp_tools.TOOLS["verb"](name="swing", args={"instance": "guest", "times": 2}),
                         {"ok": True, "result": {"crossed": False}})
        self.assertEqual(session.calls, [("client", 2)])
        self.assertEqual(session.recorded, ["verb"])
        self.assertFalse(mcp_tools.TOOLS["verb"](name="_private")["ok"])
        self.assertEqual(mcp_tools.TOOLS["verbs"](match="swing")["verbs"], ["swing(instance, times): Swings."])

    def test_a_big_result_goes_to_a_file_with_its_outline(self):
        record = {"name": "Prisoner", "level": 1, "tints": [{"name": "x" * 100}] * 40}
        shown = mcp_tools._jsonable(record)
        self.assertEqual(shown["outline"], {"name": "Prisoner", "level": 1, "tints": "<list, 40>"})
        self.assertEqual(json.loads(Path(shown["file"]).read_text()), record)
        self.assertEqual(mcp_tools._jsonable({"small": 1}), {"small": 1})

    def test_rounds_gives_a_line_each_records_itself_only_and_stops_at_until(self):
        session = FakeSession()
        registered(session)
        result = mcp_tools.TOOLS["rounds"](call="swing", count=10, until="crossed.: true")
        self.assertEqual(result["hit"], 3)
        self.assertEqual([line.split()[1] for line in result["lines"]], ["ok", "ok", "FAIL", "ok"])
        self.assertIn("missed", result["lines"][2])
        self.assertEqual(len(session.calls), 4)
        self.assertEqual(session.recorded, ["rounds"])
        self.assertEqual(len(Path(result["file"]).read_text().splitlines()), 4)

    def test_an_unexpected_error_comes_back_with_where_and_the_server_stays_up(self):
        session = FakeSession()
        registered(session)
        result = mcp_tools.TOOLS["verb"](name="swing", args={"wrong": 1})
        self.assertFalse(result["ok"])
        self.assertIn("TypeError", result["error"])
        self.assertTrue(result["where"])

    def test_log_reads_its_end_first_then_only_new_lines(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "tp_client.log"
            path.write_text("".join(f"[2026-11-04 10:00:0{i}.000] [info] line {i}\n" for i in range(5)))
            registered(FakeSession(path))
            first = mcp_tools.TOOLS["log"](pattern="line [13]")
            self.assertEqual(first["lines"], ["[2026-11-04 10:00:01.000] [info] line 1", "[2026-11-04 10:00:03.000] [info] line 3"])
            with path.open("a") as file:
                file.write("[2026-11-04 10:00:06.000] [warn] line 6\n")
            self.assertEqual(mcp_tools.TOOLS["log"]()["lines"], ["[2026-11-04 10:00:06.000] [warn] line 6"])
            self.assertEqual(mcp_tools.TOOLS["log"]()["lines"], [])
            self.assertEqual(mcp_tools.TOOLS["log"](since="all")["matched"], 6)


class Trace(unittest.TestCase):
    def entry(self, time, kind, who="player", game="", **fields):
        return {"time": time, "kind": kind, "who": who, "game": game, "f": 1, **fields}

    def test_lines_leave_out_noise_positions_and_unchanged_states_and_count_from_the_origin(self):
        entries = [
            self.entry(1.0, "graph", event="weaponSwing"),
            self.entry(9.0, "graph", event="FootLeft"),
            self.entry(9.1, "pos", pos=[0, 0, 0]),
            self.entry(10.0, "action", event="pa_KillMoveA", action=0x1234),
            self.entry(10.1, "state", state={"kill_move": True, "health": 5}),
            self.entry(10.2, "state", state={"kill_move": True, "health": 4}),
            self.entry(10.5, "graph", game="client", who="other", event="KillActor", project="Character"),
            self.entry(10.6, "graph", event="KillActor", project="FirstPerson"),
        ]
        shown = trace_lines(entries, origin="^pa_")
        self.assertEqual(shown["lines"], ["  0.000 host   player f1 action pa_KillMoveA 1234",
                                          "  0.100 host   player f1 state kill_move=True",
                                          "  0.500 client other f1 graph KillActor"])
        self.assertEqual(trace_lines(entries, who=["other"])["total"], 1)
        clips = [self.entry(10.0 + i / 10, "clips", clips=["Idle", "pa_X"], detail={"Idle": {}}) for i in range(3)]
        self.assertEqual(trace_lines(clips)["lines"], ["  0.000 host   player f1 clips Idle pa_X"])
        self.assertEqual(trace_lines(entries, origin="^pa_", grep="Kill", limit=1)["shown"], 1)


class NestedTraces(unittest.TestCase):
    def test_a_trace_inside_another_shares_the_recorder_and_leaves_it_running(self):
        from coop_harness.objects import AnimTrace

        class Recorder:  # the harness's one anim_trace per game: start adds actors, every call takes what's new
            def __init__(self):
                self.pending, self.stopped, self.t = [], False, 0

            def record(self, actor, event):
                self.t += 1
                self.pending.append({"t": self.t * 1000, "f": self.t, "kind": "graph", "actor": actor, "event": event})

            def rpc(self, request, timeout=3.0):
                self.stopped = self.stopped or request.get("stop", False)
                taken, self.pending = self.pending, []
                return {"ok": True, "entries": taken}

        class Ctx:
            def __init__(self):
                self.recorder = Recorder()
                self.host = self.recorder

            def game(self, name):
                return self.recorder

            def failure(self, text):
                return TestFailure(text)

        ctx = Ctx()
        with tempfile.TemporaryDirectory() as folder, patch("coop_harness.objects.TRACES", Path(folder)), \
             patch.object(AnimTrace, "_logs"):
            outer = AnimTrace(ctx, "outer", {"": {0x14: "player"}}, video=False).__enter__()
            ctx.recorder.record(0x14, "before")
            with AnimTrace(ctx, "inner", {"": {0x14: "me", 0x99: "other"}}, video=False) as inner:
                ctx.recorder.record(0x14, "during")
                ctx.recorder.record(0x99, "other's")
                inner.take()
            self.assertFalse(ctx.recorder.stopped, "the outer trace still records")
            ctx.recorder.record(0x14, "after")
            outer.take()
            self.assertEqual([e["event"] for e in outer.entries], ["before", "during", "after"])
            self.assertEqual([e["event"] for e in inner.entries], ["during", "other's"])
            outer.__exit__(None, None, None)
            self.assertTrue(ctx.recorder.stopped)


class Handles(unittest.TestCase):
    def test_player_handles_follow_a_new_copy_after_a_respawn(self):
        from coop_harness.actions.repro import ReproActions
        ctx = unittest.mock.MagicMock()
        ctx.REPRO_HANDLES = {"host_player": {"host": 0x14, "guest": 0xFF000832}}
        ctx.handles.side_effect = lambda: ctx.REPRO_HANDLES
        ctx.worlds.return_value = ({}, {})
        ctx.player_copy.side_effect = lambda game: 0xFF00084D if game == "client" else 0xFF000835
        ctx.name_players.side_effect = lambda: ReproActions.name_players(ctx)
        ctx.name_handle.side_effect = lambda name, ids: ctx.REPRO_HANDLES.__setitem__(name, ids)
        ReproActions.refresh_handles(ctx)
        self.assertEqual(ctx.REPRO_HANDLES["host_player"], {"host": 0x14, "guest": 0xFF00084D})


class Stays(unittest.TestCase):
    def ctx(self, rows):
        ctx = unittest.mock.MagicMock()
        ctx.failure.side_effect = lambda text: TestFailure(text)
        ctx.actor_row.side_effect = lambda instance, ref: rows.pop(0) if len(rows) > 1 else rows[0]
        return ctx

    def test_stays_fails_when_the_field_changes_inside_the_window_and_actor_does_not(self):
        from coop_harness.actions.repro import ReproActions
        alive, dead = {"state": {"dead": False}}, {"state": {"dead": True}}
        with patch("coop_harness.actions.repro.time.sleep"):
            with self.assertRaisesRegex(TestFailure, "became .*True"):
                ReproActions.expect_actor(self.ctx([alive, alive, dead, alive]), "p", "state.dead", "host", False, 1.0, stays=True)
            self.assertEqual(ReproActions.expect_actor(self.ctx([dead, alive]), "p", "state.dead", "host", False, 1.0),
                             {"state.dead": {"host": False}})


class Export(unittest.TestCase):
    def test_a_verb_exports_as_its_call_and_rounds_as_a_loop(self):
        self.assertEqual(repro._line("verb", {"name": "kill_move_exchange", "args": {"instance": "guest", "n": 2}}),
                         "ctx.kill_move_exchange(instance='client', n=2)")
        self.assertEqual(repro._line("rounds", {"call": "swing", "args": {"times": 1}, "count": 5, "reset": "use",
                                                "reset_args": {"game": "host", "target": "door"}}),
                         "for _ in range(5):\n    ctx.use('', 'door')\n    ctx.swing(times=1)")
        self.assertIsNone(repro._line("rounds", {"call": "observe", "args": {"names": ["x"]}}))

    def test_the_scenario_is_stamped_with_its_session_and_loops_are_indented(self):
        entries = [{"tool": "rounds", "args": {"call": "swing", "count": 2}, "ok": True}]
        text = repro.export_scenario(entries, "x", "Doc.", ["recorded"], recorded="20261104-x.jsonl")
        self.assertIn("# recorded: 20261104-x.jsonl, 1 calls", text)
        self.assertIn("        for _ in range(2):\n            ctx.swing()\n", text)
        compile(text, "x.py", "exec")

    def test_rounds_replays_the_rounds_it_ran_and_a_failing_expect_verb_stays(self):
        entries = [{"tool": "rounds", "args": {"call": "swing", "count": 10, "until": "open"}, "ok": True,
                    "result": {"ok": True, "lines": ["0 ok {}"], "hit": 0, "ran": 1}},
                   {"tool": "verb", "args": {"name": "expect", "args": {"name": "door", "check": "agree"}}, "ok": False},
                   {"tool": "verb", "args": {"name": "swing", "args": {}}, "ok": False}]
        text = repro.export_scenario(entries, "x", "Doc.", ["recorded"])
        self.assertIn("for _ in range(1):", text)
        self.assertIn("ctx.expect(name='door', check='agree')", text)
        self.assertEqual(text.count("ctx.swing()"), 1, "a failed verb that is not a check is left out")

    def test_a_scenario_using_the_players_handles_names_them_first(self):
        entries = [{"tool": "expect", "args": {"name": "guest_player", "check": "stays", "field": "state.dead", "value": False}, "ok": False}]
        self.assertIn("try:\n        ctx.name_players()\n        ctx.expect('guest_player'", repro.export_scenario(entries, "x", "D.", []))

    def test_every_tool_replays_or_is_known_to_only_look(self):
        samples = {"game": "host", "target": "t", "name": "n", "check": "agree", "spec": {}, "frames": 1, "line": "x",
                   "request": {"cmd": "act"}, "key": "w", "item": "1", "step": {}, "save": "s", "command": "c", "call": "use"}
        for name in mcp_tools.TOOLS:
            with self.subTest(tool=name):
                self.assertTrue(name in repro.LOOKS or repro._line(name, {**samples, "args": {}}) is not None,
                                f"{name}: give it a scenario line (repro._line) or list it in repro.LOOKS")


class DuoRefusal(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.home = Path(self.folder.name)
        self.token = Token(self.home / "duo-tokens")
        self.environment = patch.dict(os.environ, {"COOP_DUO_HOME": str(self.home)})
        self.environment.start()
        os.environ.pop(game_module.FREE_ENV, None)

    def tearDown(self):
        self.environment.stop()
        self.token.close()
        self.folder.cleanup()

    def hold(self, pid, joined=()):
        holder = {"pid": pid, "token": str(self.token.path), "joined": [{"pid": p, "token": str(self.token.path)} for p in joined]}
        (self.home / "duo.json").write_text(json.dumps({"holder": holder, "queue": []}))

    def test_the_holder_its_joined_and_their_children_hold_it(self):
        self.hold(os.getpid())
        self.assertTrue(holds_duo(self.home))
        self.hold(1, joined=[os.getppid()])
        self.assertTrue(holds_duo(self.home))
        self.hold(1)
        self.assertFalse(holds_duo(self.home))
        self.hold(os.getpid())
        self.token.close()
        self.assertFalse(holds_duo(self.home), "a dead token holds nothing")

    @unittest.skipIf(game_module.WINDOWS, "no duo lock on Windows")
    def test_a_bare_session_may_read_but_not_drive_without_the_duo(self):
        self.hold(1)
        with patch.object(game_module, "FRONTS", set()):
            with self.assertRaisesRegex(TestFailure, "skyrim-coop MCP"):
                game_module.check_duo_held({"cmd": "input"})
            game_module.check_duo_held({"cmd": "status"})
            with patch.dict(os.environ, {game_module.FREE_ENV: "1"}):
                game_module.check_duo_held({"cmd": "input"})
            self.hold(os.getpid())
            game_module.check_duo_held({"cmd": "input"})
        self.hold(1)
        with patch.object(game_module, "FRONTS", {"coop_test.py"}):
            game_module.check_duo_held({"cmd": "input"})


    @unittest.skipIf(game_module.WINDOWS, "no duo lock on Windows")
    def test_losing_the_hold_refuses_the_next_command_at_once(self):
        with patch.object(game_module, "FRONTS", set()):
            self.hold(os.getpid())
            game_module.check_duo_held({"cmd": "input"})
            (self.home / "duo.json").write_text(json.dumps({"holder": None, "queue": []}))
            with self.assertRaises(TestFailure):
                game_module.check_duo_held({"cmd": "input"})

    @unittest.skipIf(game_module.WINDOWS, "no duo lock on Windows")
    def test_diagnostic_reads_pass_and_reads_that_set_do_not(self):
        self.hold(1)
        with patch.object(game_module, "FRONTS", set()):
            for command in ("quest", "graph_bool", "gait", "perf", "global", "skill", "script_var", "trainings"):
                game_module.check_duo_held({"cmd": command})
            for request in ({"cmd": "script_var", "value": 1}, {"cmd": "trainings", "set": 2}):
                with self.assertRaises(TestFailure):
                    game_module.check_duo_held(request)


class Reload(unittest.TestCase):
    def test_reload_keeps_the_session_and_an_import_error_keeps_the_old_code(self):
        try:
            import mcp  # noqa: F401
        except ImportError:
            self.skipTest("the mcp package is not installed")
        script = r'''
import importlib, importlib.util, sys
spec = importlib.util.spec_from_file_location("coop_mcp", sys.argv[1])
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)
session = server.state["session"]
session.REPRO_HANDLES = {"lydia": {"host": 1}}
before = type(session)
added, removed = server.reload_harness()
assert type(session) is not before and type(session).__name__ == "Session", "moved onto the new class"
assert session.handles() == {"lydia": {"host": 1}}, "state kept"
assert (added, removed) == ([], []) and "verb" in server.tools
import coop_harness.errors, coop_harness.duo_lock
kept = sys.modules["coop_harness"]
real = importlib.import_module
importlib.import_module = lambda name: (_ for _ in ()).throw(SyntaxError("broken")) if name.endswith("mcp_tools") else real(name)
try:
    server.reload_harness()
    raise AssertionError("the broken import passed")
except SyntaxError:
    pass
assert sys.modules["coop_harness"] is kept, "the old code stays"
count = len(server.tools)
def schema_fails(name):  # imports fine, then its registration fails (a tool FastMCP can't describe)
    module = real(name)
    if name.endswith("mcp_tools"):
        module.register = lambda srv, state: (_ for _ in ()).throw(TypeError("bad schema"))
    return module
importlib.import_module = schema_fails
try:
    server.reload_harness()
    raise AssertionError("the bad registration passed")
except TypeError:
    pass
importlib.import_module = real
import asyncio
listed = [tool.name for tool in asyncio.run(server.server.list_tools())]
assert len(server.tools) == count and "duo_stop" in listed, "the old tools stay registered"
assert sys.modules["coop_harness"] is kept
print("reload ok", file=sys.stderr)
'''
        done = subprocess.run([sys.executable, "-c", script, str(Path(__file__).resolve().parent / "coop_mcp.py")],
                              capture_output=True, text=True, timeout=60, stdin=subprocess.DEVNULL)
        self.assertEqual(done.returncode, 0, done.stderr[-2000:])
        self.assertIn("reload ok", done.stderr)


class ScenarioOrigins(unittest.TestCase):
    def test_new_flows_say_recorded_or_hand_written(self):
        diff = ("+    registry.add('a', [], lambda ctx: f(ctx), doc='x')\n"
                "+    registry.add('b', [], lambda ctx: f(ctx), doc='x')  # hand-written: a feature\n"
                " registry.add('c', ...)\n")
        files = {"r/x.py": '"""d"""\n# recorded: 1-x.jsonl, 3 calls\nNAME = 1', "r/y.py": '"""d"""\nNAME = 2', "r/__init__.py": ""}
        self.assertEqual(check_scenarios.unstated(diff, files), [f"a ({check_scenarios.REGISTRATIONS})", "r/y.py"])


if __name__ == "__main__":
    unittest.main()
