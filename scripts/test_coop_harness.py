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
                                comparison_checks=None, keep_failure=None, replay_refusals_line="").items():
            setattr(session, name, Mock(return_value=value))
        for game in session.games:
            game.read_status = Mock(return_value={"game": game.name or "host"})
        session.observe = Mock()
        return session

    def test_dead_guest_travel_checks_the_current_enemy_copy_with_original_timeout(self):
        session = self.session
        for name in ("console_on", "give_sword", "sheathe", "place_before", "wait_for", "freeze",
                     "camera_sees_followed", "spectating_stays_third_person", "both_go", "tap_key",
                     "player_command", "draw", "attack", "remove_host_npc"):
            setattr(session, name, Mock())
        session.spawn_host_npc = Mock(return_value=(0xFF100, 13, 0xFF200))
        session.client_on_host = Mock(return_value=0xFF300)
        session.player_snapshot = Mock(return_value={"values": {"health": 5}, "state": {"hands": [0, 0]}})
        session.effect_spells = Mock(return_value=set())
        session.is_dead = Mock(return_value=True)
        session.death_state = Mock(return_value={"spectating": 1, "down": True})
        session.host_npc_state = Mock(return_value={"session_id": 22, "dead": True})
        # The pre-travel copy is absent. The current copy is already dead.
        actors = {str(0xFF201): {"session_id": 22, "dead": True, "has_3d": True}}
        session.actors_on = Mock(return_value=actors)
        class ObservedCurrentCopy(Exception):
            pass
        def wait_on(instance, description, predicate, timeout=15):
            if description == "the enemy's copy dies too":
                self.assertEqual(timeout, 5)
                if not predicate(actors):
                    raise TestFailure("original five-second check queried a stale copy")
                raise ObservedCurrentCopy()
            return predicate(actors)
        session.wait_on = wait_on
        with self.assertRaises(ObservedCurrentCopy):
            session.death_spectate_respawn("client", travel_down=True)

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

    def test_race_readout_uses_the_requested_games_status(self):
        self.session.host.read_status = Mock(return_value={"world": {"20": {"identity": {"race": 0x13746}}}})
        self.session.guest.read_status = Mock(return_value={"world": {"20": {"identity": {"race": 0x13741}}}})
        self.assertEqual(self.session.race_of("", 20), 0x13746)
        self.assertEqual(self.session.race_of("client", 20), 0x13741)

    def test_replay_refusals_include_rotated_logs(self):
        from coop_harness.common import log_time
        with tempfile.TemporaryDirectory() as directory:
            paths = {game: Path(directory) / (game or "host") for game in ("", "client")}
            paths = {game: path.with_suffix(".log") for game, path in paths.items()}
            self.session.plugin_log = lambda game: paths[game]
            paths["client"].with_name("client.1.log").write_text(
                "[2026-10-30 15:00:00.000] [debug] Copy FF001234 refused replayed action 7F8E3 'IdleStopInstant' (weapon state 0)\n")
            line = self.session.replay_refusals_line(log_time("2026-10-30 14:59:59.000"))
            self.assertIn("client FF001234 IdleStopInstant x1", line)
            self.assertFalse(self.session.replay_refusals_line(log_time("2026-10-30 15:00:01.000")))

    def test_refusals_are_reported_when_an_action_fails(self):
        session = self.runner_fixture(lambda ctx: (_ for _ in ()).throw(ctx.failure("failed action")))
        session.replay_refusals_line = Mock(return_value="client FF001234 IdleStopInstant x1")
        with patch("coop_harness.runner.log") as output:
            result = session.run_flow("first", {})
        self.assertTrue(result.failures)
        output.assert_any_call("copies refused replayed animations: client FF001234 IdleStopInstant x1")

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

    def batch_fixture(self):
        session = self.runner_fixture()
        session.load_home = Mock(return_value={"world": {}})
        session.upgrade_home = Mock()
        session.trail_marks = Mock(return_value={})
        session.failure_digest = Mock(return_value="original failure")
        session.restart_duo = Mock(return_value={})
        return session

    def run_batch(self, session, **options):
        args = argparse.Namespace(names=[], tag=None, hold=0, repeat=1, shuffle=False,
                                  seed=None, fail_fast=False, dry_run=False, **options)
        with contextlib.redirect_stdout(io.StringIO()) as output, self.assertRaises(SystemExit):
            session.command_scenario(args)
        return output.getvalue()

    def test_batch_stops_after_unrestorable_world_without_expanding_allowlist(self):
        session = self.batch_fixture()
        session.check_world.side_effect = [{}, {"door": {"lock": "differs"}}]
        output = self.run_batch(session, no_restart=True)
        self.assertIn("NOT RUN (first left the games off the baseline)", output)
        self.assertEqual(session.comparison_checks.call_count, 1)
        session.restart_duo.assert_not_called()

    def test_batch_boots_the_duo_again_after_unrestorable_world_and_goes_on(self):
        session = self.batch_fixture()
        session.check_world.side_effect = [{}, {"door": {"lock": "differs"}}, {}]
        output = self.run_batch(session)
        session.restart_duo.assert_called_once()
        self.assertEqual(session.comparison_checks.call_count, 2)
        self.assertIn("broke the baseline: duo booted again", output)
        self.assertRegex(output, r"next\s+\S+s\s+PASS")
        self.assertIn("baseline broken by           first", output)

    def test_batch_stops_when_booting_again_fails(self):
        session = self.batch_fixture()
        session.check_world.side_effect = [{}, {"door": {"lock": "differs"}}]
        session.restart_duo.side_effect = TestFailure("the duo did not boot again")
        output = self.run_batch(session)
        self.assertIn("NOT RUN (first left the games off the baseline; booting the duo again failed: the duo did not boot again)", output)

    def test_batch_off_the_baseline_at_start_boots_the_duo_again(self):
        session = self.batch_fixture()
        session.settle_baseline.side_effect = [["host: not in the baseline cell"], [], []]
        session.comparison_checks.side_effect = TestFailure("fails anyway")
        self.run_batch(session)
        session.restart_duo.assert_called_once()
        self.assertEqual(session.comparison_checks.call_count, 2)

    def test_restart_boots_with_the_recorded_arguments(self):
        session = self.runner_fixture()
        with tempfile.TemporaryDirectory() as directory:
            session.host.control = Path(directory)
            boot = {"cell": "QASmoke", "save": None, "level": None, "npc": "", "seconds": 30,
                    "boot_timeout": 240, "world_timeout": 180}
            (Path(directory) / "boot.json").write_text(json.dumps(boot))
            session.stop_duo, session.command_duo = Mock(), Mock()
            session.restart_duo()
            session.stop_duo.assert_called_once()
            booted = session.command_duo.call_args.args[0]
            self.assertEqual((booted.cell, booted.keep_running, booted.boot_timeout), ("QASmoke", True, 240))
            (Path(directory) / "boot.json").unlink()
            with self.assertRaisesRegex(TestFailure, "no boot.json"):
                session.restart_duo()

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

    def test_sliding_actors_are_those_that_move_without_walking(self):
        from coop_harness.comparisons import sliding_actors
        gait = {"1": {"travelled": 160, "feet": 2}, "2": {"travelled": 160, "feet": 170},
                "3": {"travelled": 550, "feet": 400}, "4": {"travelled": 40, "feet": 0}, "5": {"travelled": 300, "feet": 60}}
        self.assertEqual(sliding_actors(gait), {1: (160, 2), 5: (300, 60)})

    def test_discovery_commands_need_no_running_games(self):
        scripts = Path(__file__).resolve().parent
        for command in ([sys.executable, str(scripts / "coop_test.py"), "scenario", "--dry-run", "--tag", "inventory"],
                        [sys.executable, str(scripts / "coop_specs.py"), "--dry"]):
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
