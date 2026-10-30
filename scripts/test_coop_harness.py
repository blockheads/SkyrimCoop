"""Harness ownership, selection, checkpoints and failure cleanup, without Skyrim."""
import argparse
import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

from coop_harness import Session
from coop_harness.errors import TestFailure
from coop_harness.game import Game
from coop_harness.registry import Registry
from coop_harness.scenarios import specs


class Harness(unittest.TestCase):
    def setUp(self):
        self.session = Session()
        self.addCleanup(self.session.close)

    def runner_fixture(self, action=lambda session: None):
        session = self.session
        session.registry = Registry()
        session.registry.add("first", ["inventory"], action)
        session.registry.add("next", ["inventory"], lambda session: None)
        for name, value in dict(lock_order_reports={}, check_dialogue_trace=[], settle_baseline=[], check_world={},
                                comparison_checks=None, keep_failure=None).items():
            setattr(session, name, Mock(return_value=value))
        for game in session.games:
            game.read_status = Mock(return_value={"game": game.name or "host"})
        session.observe = Mock()
        return session

    def test_registration_is_unique_and_has_a_fixture(self):
        registry = Registry()
        registry.add("ordinary", ["inventory"], lambda session: None)
        registry.add("story", ["late-save"], lambda session: None)
        self.assertEqual(registry["ordinary"].fixture, "baseline")
        self.assertEqual(registry["story"].fixture, "saved-story")
        with self.assertRaisesRegex(ValueError, "duplicate flow"):
            registry.add("ordinary", ["hands"], lambda session: None)

    def test_tags_select_each_canonical_flow_once_and_old_names_are_rejected(self):
        names = self.session.scenario_names([], "hands,inventory")
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(names.count("player-equipment-host"), 1)
        self.assertEqual(self.session.scenario_names(["player-equipment-host"] * 2, None), ["player-equipment-host"])
        for obsolete in ("equip-host-player", "equip-client-player", "draw-host-player", "draw-client-player",
                         "equip-host-npc", "repair-equipment-drift"):
            with self.assertRaises(TestFailure):
                self.session.scenario_names([obsolete], None)

    def test_fixture_and_account_selection_boundaries_survive(self):
        session = self.session
        default = session.scenario_names([], None)
        self.assertTrue(all("input" not in session.registry[n].tags and "late-save" not in session.registry[n].tags for n in default))
        tagged = session.scenario_names([], "session")
        self.assertTrue(all("restart" not in session.registry[n].tags for n in tagged))
        with self.assertRaisesRegex(TestFailure, "unknown tag"):
            session.scenario_names([], "does-not-exist")

    def test_games_own_paths_connections_and_do_not_change_one_another(self):
        session = self.session
        self.assertNotEqual(session.host.control, session.guest.control)
        self.assertNotEqual(session.host.prefix, session.guest.prefix)
        self.assertEqual((session.host.display, session.guest.display), (":99", ":100"))
        session.host.rpc_connections["host"] = [None, Mock(), Mock()]
        session.guest.rpc_connections["guest"] = [None, Mock(), Mock()]
        session.host.held_buttons.add("right")
        self.assertFalse(session.guest.held_buttons)
        session.host.close()
        self.assertTrue(session.guest.rpc_connections)
        with self.assertRaisesRegex(ValueError, "different session"):
            session.game(Game(""))

    def test_package_prefix_uses_its_game_install_and_can_skip_bundled_start(self):
        with tempfile.TemporaryDirectory() as directory:
            install = Path(directory) / "game"
            install.mkdir()
            for name in ("Skyrim_Default.ini", "Low.ini"):
                (install / name).write_text("[General]\n")
            game = Game("", install=install, prefix=Path(directory) / "prefix", display=":99")
            game.my_games.mkdir(parents=True)
            self.session.host = game
            self.session.games = (game, self.session.guest)
            self.session.enable_bundled_start = Mock()
            self.session.prepare_prefix(bundled_start=False)
            self.session.enable_bundled_start.assert_not_called()
            self.assertIn("bShowTutorials=0", (game.my_games / "Skyrim.ini").read_text())
            self.session.prepare_prefix()
            self.session.enable_bundled_start.assert_called_once_with(_game=game)

    def test_status_rejects_stale_files_and_uses_each_games_control_path(self):
        with tempfile.TemporaryDirectory() as directory:
            self.session.host.control = Path(directory)
            self.session.host.rpc = Mock(return_value=None)
            (Path(directory) / "status.json").write_text(json.dumps({"heartbeat_ms": 1}))
            self.assertIsNone(self.session.host.read_status())
            self.session.guest.rpc = Mock(return_value={"ok": True, "status": {"guest": True}})
            self.assertEqual(self.session.guest.read_status(), {"guest": True})

    def test_checkpoints_preserve_original_failure_before_cleanup(self):
        def action(session):
            session.wait_for("wears the sword", lambda: False, 0)
        session = self.runner_fixture(action)
        events = []
        session.keep_failure.side_effect = lambda name, error: events.append(("capture", error))
        session.settle_baseline.side_effect = lambda: events.append(("restore", None)) or []
        result = session.run_flow("first", set())
        phase, error = result.failures[0]
        self.assertEqual(phase, "action")
        self.assertEqual(error.checkpoint, "wears the sword")
        self.assertEqual(set(error.status), {"", "client"})
        self.assertEqual([event for event, _ in events], ["capture", "restore"])
        self.assertTrue(any(call.kwargs.get("description") == "wears the sword" for call in session.observe.call_args_list))

    def test_cleanup_failure_does_not_replace_action_failure(self):
        original = TestFailure("action failed")
        session = self.runner_fixture(Mock(side_effect=original))
        session.settle_baseline.side_effect = RuntimeError("cleanup broke")
        result = session.run_flow("first", set())
        self.assertIs(result.failures[0][1], original)
        self.assertEqual([phase for phase, _ in result.failures], ["action", "cleanup"])
        self.assertFalse(result.restored)

    def test_comparison_failure_is_distinct_and_until_fail_skips_restoration(self):
        session = self.runner_fixture()
        session.comparison_checks.side_effect = TestFailure("copy differs")
        result = session.run_flow("first", set(), until_fail=True)
        self.assertEqual(result.failures[0][0], "comparison")
        session.settle_baseline.assert_not_called()

    def test_saved_story_fixture_rejects_empty_or_qasmoke_homes(self):
        session = self.runner_fixture()
        session.registry.add("story", ["late-save"], lambda session: self.fail("story actions must not run"))
        for home in (None, {"cell_name": "QASmoke", "world": {}}, {"cell_name": None, "world": {}}):
            session.load_home = Mock(return_value=home)
            result = session.run_flow("story", set())
            self.assertEqual(result.failures[0][0], "setup")
            self.assertIn("requires duo --save", str(result.failures[0][1]))

    def test_setup_failure_still_attempts_cleanup(self):
        session = self.runner_fixture()
        session.lock_order_reports.side_effect = RuntimeError("status unavailable")
        result = session.run_flow("first", set())
        self.assertEqual(result.failures[0][0], "setup")
        session.settle_baseline.assert_called_once()

    def test_batch_stops_after_unrestorable_world_without_expanding_allowlist(self):
        session = self.runner_fixture()
        session.load_home = Mock(return_value={"world": {}})
        session.upgrade_home = Mock()
        session.trail_marks = Mock(return_value={})
        session.failure_digest = Mock(return_value="original failure")
        session.check_world.side_effect = [{}, {"door": {"lock": "differs"}}]
        args = argparse.Namespace(names=[], tag=None, hold=0, repeat=1, shuffle=False,
                                  seed=None, fail_fast=False, dry_run=False)
        with contextlib.redirect_stdout(io.StringIO()) as output, self.assertRaises(SystemExit):
            session.command_scenario(args)
        self.assertIn("next", output.getvalue())
        self.assertIn("NOT RUN (the games could not be reset", output.getvalue())
        self.assertEqual(session.comparison_checks.call_count, 1)

    def test_shared_spec_attempts_every_cleanup_and_actor_after_failure(self):
        session = self.session
        original = TestFailure("original action")
        visited = []
        def step(ctx, spec, index, value, cleanup=False, actors=None):
            visited.append(value["id"])
            if not cleanup:
                actors.update({"one": {"host": 1}, "two": {"host": 2}})
                raise original
            if index == 0:
                raise RuntimeError("first cleanup failed")
        restore = Mock(side_effect=[RuntimeError("first actor failed"), None])
        with patch.object(specs, "run_step", side_effect=step), patch.object(specs, "restore_actor", restore), self.assertRaises(TestFailure) as caught:
            specs.run(session, {"steps": [{"id": "action"}], "cleanup": [{"id": "cleanup1"}, {"id": "cleanup2"}]})
        self.assertIs(caught.exception, original)
        self.assertEqual(visited, ["action", "cleanup1", "cleanup2"])
        self.assertEqual(restore.call_count, 2)
        self.assertEqual(len(session.cleanup_errors), 2)

    def test_discovery_commands_need_no_running_games(self):
        scripts = Path(__file__).resolve().parent
        for command in ([sys.executable, str(scripts / "coop_test.py"), "scenario", "--dry-run", "--tag", "inventory"],
                        [sys.executable, str(scripts / "coop_specs.py"), "--dry"]):
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
