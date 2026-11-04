"""Harness ownership, selection, checkpoints and failure cleanup, without Skyrim."""
import argparse
import contextlib
import copy
import io
import json
import os
import subprocess
import time
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, Mock, patch

from coop_harness import Session
from coop_harness.actions.combat import CombatActions
from coop_harness.common import WINDOWS
from coop_harness.errors import TestFailure
from coop_harness.game import Game
from coop_harness.registry import Registry
from coop_harness.scenarios import dialogue, specs


class Harness(unittest.TestCase):
    def setUp(self):
        self.session = Session()
        self.addCleanup(self.session.close)

    @unittest.skipIf(WINDOWS, "isolated Linux test-game asset links")
    def test_test_game_logs_never_write_through_links_into_the_steam_install(self):
        from coop_harness import runtime
        with tempfile.TemporaryDirectory() as temp:
            source, game = Path(temp) / "steam", Path(temp) / "game"
            (source / "Data").mkdir(parents=True)
            game.mkdir()
            (source / "Debug.log").write_text("the player's log")
            (source / "Logs").mkdir()
            (game / "Debug.log").symlink_to(source / "Debug.log")
            (game / "Logs").symlink_to(source / "Logs", target_is_directory=True)
            with patch.multiple(runtime, STEAM_GAME=source, GAME=game), \
                 patch.object(runtime, "game_executable", return_value=source / "SkyrimSE.exe"), \
                 patch.object(self.session, "copy_if_changed"), \
                 patch.object(self.session, "stage_skse_scripts"), \
                 patch.object(self.session, "instance_users", return_value=[]):
                self.session.ensure_duo_game()
                self.session.ensure_duo_game()
            self.assertFalse((game / "Debug.log").is_symlink())
            self.assertFalse((game / "Logs").is_symlink())
            (game / "Debug.log").write_text("Bethesda writes its log")
            (game / "Logs").mkdir()
            (game / "Logs" / "ours.log").write_text("test log")
            self.assertEqual((source / "Debug.log").read_text(), "the player's log")
            self.assertEqual(list((source / "Logs").iterdir()), [])

    def test_dialogue_pick_completion_requires_usable_matching_menus_or_a_complete_close(self):
        session = self.session
        option = {'topic': 0x1001, 'info': 0x2001, 'quest': 0, 'text': 'News?'}
        host = {'speaker': 0x123, 'menu': False, 'options': [option], 'swf': {'state': -1, 'allow_progress': -1}}
        guest = {'speaker': 0x456, 'menu': True, 'options': [option], 'swf': {'state': 1, 'allow_progress': 1, 'cursor_index': 0}}
        states = {'': copy.deepcopy(host), 'client': copy.deepcopy(guest)}
        session.spawn_host_npc = Mock(return_value=(0x123, 3, 0x456))
        for method in ('place_before', 'tap_key', 'close_dialogues', 'remove_host_npc', 'hold'):
            setattr(session, method, Mock())
        session.activate_on = Mock()
        session.dialogue = lambda instance: states[instance]
        session.log_trail = Mock(return_value=([{'text': 'Dialogue: player 2 picked topic 1001 (info 2001)'}], 10))
        session.line_over = session.line_playing = Mock(return_value=True)
        predicates = []
        class Captured(Exception):
            pass
        def wait(description, predicate, timeout=15, **kwargs):
            if description.startswith('[both] the conversation follows the pick'):
                predicates.append(predicate)
                raise Captured()
            if description == '[client] the menu lists options':
                return [option]
            return True
        session.wait_for = wait
        with self.assertRaises(Captured):
            dialogue.scenario_dialogue_pick_reaches_host(session)
        followed = predicates[0]
        cases = [
            ('unchanged usable list can be the next step', {}, {}, True),
            ('hidden old list', {}, {'swf': {'state': 2, 'allow_progress': 1}}, False),
            ('transitioning list', {}, {'swf': {'state': 3, 'allow_progress': 1}}, False),
            ('visible list cannot accept input', {}, {'swf': {'state': 1, 'allow_progress': 0}}, False),
            ('same topic with the wrong INFO', {}, {'options': [{**option, 'info': 0x2999}]}, False),
            ('host menu is not usable', {'menu': True, 'swf': {'state': 2, 'allow_progress': 0}}, {}, False),
            ('host menu still open after releasing its speaker', {'speaker': 0, 'menu': True},
             {'speaker': 0, 'menu': False}, False),
            ('closed menu still holds its speaker', {'speaker': 0}, {'menu': False}, False),
            ('both conversations completely closed', {'speaker': 0}, {'speaker': 0, 'menu': False}, True),
        ]
        for name, host_changes, guest_changes, expected in cases:
            with self.subTest(name=name):
                states[''] = {**copy.deepcopy(host), **host_changes}
                states['client'] = {**copy.deepcopy(guest), **guest_changes}
                self.assertEqual(bool(followed()), expected)

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

    def play_vanilla(self, control="host", built=True):
        session = self.session
        session.command_launch, session.command_duo = Mock(), Mock()
        args = argparse.Namespace(vanilla=True, control=control, attach=False)
        monitors = Mock(stdout="Monitors: 1\n 0: +*DP-1 2560/600x1440/340+0+0  DP-1\n")
        harness = Mock(exists=Mock(return_value=built))
        with patch("coop_harness.commands.VANILLA_HARNESS", harness), \
                patch("coop_harness.commands.subprocess.run", return_value=monitors), \
                patch("coop_harness.commands.subprocess.Popen") as view:
            session.command_play(args)
        return args, view

    def test_play_vanilla_boots_one_game_in_a_window_and_no_view(self):
        args, view = self.play_vanilla()
        self.session.command_launch.assert_called_once_with(args, _game=self.session.host)
        self.session.command_duo.assert_not_called()
        self.assertEqual(self.session.VISIBLE, {""})
        view.assert_not_called()

    def test_play_vanilla_refuses_a_guest_and_a_missing_stand_in(self):
        with self.assertRaisesRegex(TestFailure, "solo"):
            self.play_vanilla(control="guest")
        with self.assertRaisesRegex(TestFailure, "SkyrimCoopVanillaHarness"):
            self.play_vanilla(built=False)
        self.session.command_launch.assert_not_called()

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
            if WINDOWS:  # not the user's own My Games
                game.my_games = Path(directory) / "My Games"
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

    @unittest.skipIf(WINDOWS, "game users and ACLs")
    def test_the_games_user_can_write_my_games_this_user_creates(self):
        # A fresh worktree's My Games was this user's only: SKSE (run as coop2) could not log, CrashLogger failed to load.
        session = self.session
        with tempfile.TemporaryDirectory() as temp:
            game = Game("", install=Path(temp) / "game", prefix=Path(temp) / "prefix-host")
            (game.prefix / "pfx/drive_c/users/steamuser").mkdir(parents=True)
            (game.install).mkdir()
            (game.install / "Skyrim_Default.ini").write_text("[General]\n")
            (game.install / "Low.ini").write_text("[Display]\n")
            session.games = (game, session.guest)
            session.host = game
            calls = []
            with patch.object(session, "instance_user", return_value="coop2"), patch.object(session, "own_prefix"), \
                 patch.object(session, "sudo", side_effect=lambda *command: calls.append(command)), \
                 patch.object(session, "enable_bundled_start"):
                with contextlib.suppress(Exception):  # the rest of the preparation is not this test's
                    session.prepare_prefix(_game=game)
            self.assertIn(("setfacl", "-R", "-m", "u:coop2:rwX,d:u:coop2:rwX", str(game.my_games)), calls)

    @unittest.skipIf(WINDOWS, "Wine audio drivers")
    def test_games_run_as_another_user_play_to_the_null_audio_device(self):
        # dive-*, copy-ends-voice-line: with no audio driver Skyrim queues voice lines that never start.
        session = self.session
        with patch.object(session, "instance_user", return_value="coop2"), patch.object(session, "shown", return_value=True), \
             patch.dict("os.environ", {"WINEDLLOVERRIDES": "dxgi=n"}):
            for game in (session.host, session.guest):
                env = session.game_env(_game=game)
                overrides = env["WINEDLLOVERRIDES"].split(";")
                self.assertIn("winepulse.drv=d", overrides)
                self.assertNotIn("winealsa.drv=d", overrides)
                self.assertIn("dxgi=n", overrides)
                config = Path(env["ALSA_CONFIG_PATH"])
                self.assertIn("pcm.!default { type null }", config.read_text())
                self.assertTrue(config.stat().st_mode & 0o004)

    @unittest.skipIf(WINDOWS, "Wine audio drivers")
    def test_games_run_as_this_user_keep_their_audio(self):
        session = self.session
        with patch.object(session, "instance_user", return_value=None), patch.object(session, "shown", return_value=True), \
             patch.dict("os.environ", {"WINEDLLOVERRIDES": "dxgi=n"}):
            os.environ.pop("ALSA_CONFIG_PATH", None)
            env = session.game_env(_game=session.host)
        self.assertEqual(env["WINEDLLOVERRIDES"], "dxgi=n")
        self.assertNotIn("ALSA_CONFIG_PATH", env)

    def test_a_timeout_says_what_was_seen_and_a_crash_stays_as_it_is(self):
        session = self.session
        with patch.object(session, "check_for_crash"), patch.object(session, "observe"):
            with self.assertRaisesRegex(TestFailure, r"Timed out after 0s: surfaces \(head under\)"):
                session.wait_for("surfaces", lambda: False, 0.01, explain=lambda: "head under")
        crash = TestFailure("client crashed: report")
        with patch.object(session, "check_for_crash", side_effect=crash), patch.object(session, "observe"):
            with self.assertRaises(TestFailure) as raised:
                session.wait_for("surfaces", lambda: False, 5, explain=lambda: "head under")
        self.assertIs(raised.exception, crash)

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

    def test_quit_recipe_fails_on_a_crash_even_after_the_process_exits(self):
        from coop_harness.scenarios import quit
        session = self.session
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            crash = directory / "crash-new.log"
            crash.write_text("VM teardown crash")
            session.check_prerequisites = Mock()
            session.new_run_dir = Mock(return_value=directory / "evidence")
            (directory / "evidence").mkdir()
            session.launch = Mock()
            session.wait_for = Mock(side_effect=lambda _, predicate, *args, **kw: predicate())
            session.prefix_processes = Mock(return_value=[])
            session.game_process = Mock(return_value=999999999)
            session.game_exited = Mock(return_value=True)
            session.stop_game = Mock(return_value=True)
            session.collect_logs = Mock()
            crashed = False
            session.crash_logs = Mock(side_effect=lambda _: [crash] if crashed else [])

            def confirm(*args, **kwargs):
                nonlocal crashed
                crashed = True

            with patch.object(quit, "quit_to_desktop", side_effect=confirm):
                with self.assertRaisesRegex(TestFailure, "Quit crashed"):
                    quit.reproduce(session, argparse.Namespace(runs=1), main_menu=True, game=session.host)
            session.stop_game.assert_called_once_with(_game=session.host)
            self.assertTrue((directory / "evidence" / crash.name).exists())

    def quit_fixture(self, directory):
        session = self.session
        session.check_prerequisites = Mock()
        session.new_run_dir = Mock(return_value=directory)
        session.launch = Mock()
        session.prefix_processes = Mock(return_value=[])
        session.game_process = Mock(return_value=999999999)
        session.game_exited = Mock(return_value=True)
        session.crash_logs = Mock(return_value=[])
        session.stop_game = Mock(return_value=True)
        session.collect_logs = Mock()

        def wait(description, predicate, *args, **kwargs):
            if not (value := predicate()):
                raise session.failure(f"Timed out: {description}")
            return value
        session.wait_for = Mock(side_effect=wait)
        return session

    def test_quit_recipe_preserves_games_already_running(self):
        from coop_harness.scenarios import quit
        with tempfile.TemporaryDirectory() as tmp:
            session = self.quit_fixture(Path(tmp))
            session.prefix_processes = Mock(side_effect=lambda *, _game: [123] if _game is session.guest else [])
            session.launch.side_effect = session.failure("The test Skyrim is already running")
            with patch.object(quit, "quit_to_desktop"), self.assertRaisesRegex(TestFailure, "already running"):
                quit.reproduce(session, argparse.Namespace(runs=1), main_menu=True, game=session.host)
            session.launch.assert_not_called()
            session.stop_game.assert_not_called()
            session.check_prerequisites.assert_not_called()

    def test_quit_recipe_does_not_pass_when_the_platform_reports_skyrim_alive(self):
        from coop_harness.scenarios import quit
        with tempfile.TemporaryDirectory() as tmp:
            session = self.quit_fixture(Path(tmp))
            session.game_exited.return_value = False
            with patch.object(quit, "quit_to_desktop"), self.assertRaisesRegex(TestFailure, "Timed out"):
                quit.reproduce(session, argparse.Namespace(runs=1), main_menu=True, game=session.host)
            session.game_exited.assert_called_once_with(session.host.name, 999999999)

    def test_quit_recipe_checks_unclassified_windows_crash_logs_before_cleanup(self):
        from coop_harness.scenarios import quit
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            session = self.quit_fixture(directory / "evidence")
            (directory / "evidence").mkdir()
            session.host.my_games = directory / "Documents"
            crashes = session.host.my_games / "SKSE"
            crashes.mkdir(parents=True)
            crash = crashes / "crash-new.log"
            with patch.object(quit, "quit_to_desktop", side_effect=lambda *args, **kwargs: crash.write_text("VM teardown crash")), \
                    self.assertRaisesRegex(TestFailure, "Quit crashed"):
                quit.reproduce(session, argparse.Namespace(runs=1), main_menu=True, game=session.host)
            self.assertTrue((directory / "evidence" / crash.name).exists())

    def test_windows_quit_tracks_the_recorded_process_identity(self):
        with patch.dict(sys.modules, {"_winapi": Mock()}):
            from coop_harness.windows import WindowsRuntime
        context = Mock()
        context.recorded_process.return_value = Mock(pid=123)
        self.assertFalse(WindowsRuntime.game_exited(context, "", 123))
        context.recorded_process.return_value = None
        self.assertTrue(WindowsRuntime.game_exited(context, "", 123))
        context.recorded_process.return_value = Mock(pid=456)
        self.assertTrue(WindowsRuntime.game_exited(context, "", 123))

    @unittest.skipUnless(sys.platform == "linux", "Linux process adapter")
    def test_linux_quit_distinguishes_a_live_process_from_an_exited_one(self):
        import os
        self.assertFalse(self.session.game_exited("", os.getpid()))
        self.assertTrue(self.session.game_exited("", 999999999))

    def test_quit_recipe_fails_if_its_game_cannot_be_stopped(self):
        from coop_harness.scenarios import quit
        with tempfile.TemporaryDirectory() as tmp:
            session = self.quit_fixture(Path(tmp))
            session.stop_game.return_value = False
            with patch.object(quit, "quit_to_desktop"), self.assertRaisesRegex(TestFailure, "could not stop"):
                quit.reproduce(session, argparse.Namespace(runs=1), main_menu=True, game=session.host)

    def test_discovery_commands_need_no_running_games(self):
        scripts = Path(__file__).resolve().parent
        for command in ([sys.executable, str(scripts / "coop_test.py"), "scenario", "--dry-run", "--tag", "inventory"],
                        [sys.executable, str(scripts / "coop_specs.py"), "--dry"]):
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.strip())


class LoadMenu(unittest.TestCase):
    """load_latest_from_menu against a fake System page: E steps it through its states."""
    def load(self, first_state, saves):
        from coop_harness.actions.join import JoinActions
        state = {"now": 0}
        def tap(_, key):
            if key == 0x12:
                state["now"] = {0: first_state, 17: 1, 1: 2}[state["now"]]
        def menu_var(_, __, path):
            if path.endswith("iCurrentTab"):
                return 2
            if path.endswith("iCurrentState"):
                return state["now"]
            if path.endswith("selectedIndex"):
                return 0
            if path.endswith("entryList.length"):
                return 1
            return "$LOAD"
        def wait_for(what, check, *_):
            if not check():
                raise TestFailure(what)
        ctx = MagicMock()
        ctx.failure.side_effect = lambda *args: TestFailure(*args)
        ctx.game.return_value.my_games = saves.parent
        ctx.menus.return_value = ["Journal Menu"]
        ctx.menu_var.side_effect, ctx.tap_key.side_effect, ctx.wait_for.side_effect = menu_var, tap, wait_for
        JoinActions.load_latest_from_menu(ctx, "")
        return state["now"]

    def saves(self, root):
        saves = Path(root) / "Saves"
        saves.mkdir()
        for name in ("SkyrimCoopWipe.ess", "SkyrimCoopWipe.skse", "Quicksave0.ess", "Quicksave0.skse"):
            (saves / name).touch()
        return saves

    def test_the_character_list_leads_to_the_saves(self):
        with tempfile.TemporaryDirectory() as root:
            self.assertEqual(self.load(17, self.saves(root)), 2)

    def test_a_game_going_straight_to_its_saves_still_loads(self):
        with tempfile.TemporaryDirectory() as root:
            self.assertEqual(self.load(1, self.saves(root)), 2)

    def test_a_reloads_scratch_save_goes_and_the_players_stay(self):
        with tempfile.TemporaryDirectory() as root:
            saves = self.saves(root)
            self.load(1, saves)
            self.assertEqual(sorted(path.name for path in saves.iterdir()), ["Quicksave0.ess", "Quicksave0.skse"])


class GoalVerbs(unittest.TestCase):
    def verb_ctx(self, entries=()):
        ctx = MagicMock()
        ctx.failure.side_effect = lambda *args: TestFailure(*args)
        trace = ctx.verb_trace.return_value.__enter__.return_value
        trace.entries, trace.dir = list(entries), Path("/traces/x")
        ctx.verb_trace.return_value.__exit__.return_value = False
        return ctx

    def test_a_trace_sums_up_speeds_and_where_the_player_stood(self):
        from coop_harness.actions.play_verbs import trace_summary
        pos = lambda t, x: {"kind": "pos", "who": "me", "time": t, "pos": [x, 0, 0]}
        moving = [pos(i * 0.25, i * 100.0) for i in range(5)]
        stopped = [pos(1.0 + i * 0.25, 400.0) for i in range(1, 7)]
        again = [pos(2.5 + i * 0.25, 400.0 + i * 100.0) for i in range(1, 4)]
        graph = [{"kind": "graph", "who": "me", "event": f"e{i}"} for i in range(8)]
        summary = trace_summary(moving + stopped + again + graph)
        self.assertEqual(summary["speed"]["max"], 400)
        self.assertEqual(summary["speed"]["min"], 0)
        self.assertEqual(summary["stalls"], [[1.0, 1.5]])
        self.assertEqual(summary["events"], ["e2", "e3", "e4", "e5", "e6", "e7"])
        self.assertEqual(trace_summary([])["stalls"], [])

    def test_a_walk_that_ran_out_of_time_says_why(self):
        from coop_harness.actions.play_verbs import walk_why
        self.assertEqual(walk_why({"closest": 100, "stalls": 2}, 900), "moved_off")
        self.assertEqual(walk_why({"closest": 100, "stalls": 2}, 150), "stalled")
        self.assertEqual(walk_why({"closest": 100, "stalls": 0}, 150), "timeout")

    def test_a_goal_verb_keeps_its_outcome_and_a_failure_carries_it(self):
        from coop_harness.actions.play_verbs import goal, verb_failure
        ctx = self.verb_ctx()

        @goal
        def fine(ctx, instance, target):
            return {"how": "run"}

        @goal
        def blocked(ctx, instance, target):
            raise verb_failure(ctx, "no_path", "no way", closest=300)

        outcome = fine(ctx, "client", "lydia")
        self.assertEqual((outcome["done"], outcome["how"], outcome["game"], outcome["verb"]), (True, "run", "guest", "fine"))
        self.assertIs(ctx.LAST_VERB, outcome)
        with self.assertRaises(TestFailure) as caught:
            blocked(ctx, "", "lydia")
        report = caught.exception.outcome
        self.assertEqual((report["done"], report["why"], report["closest"], report["failure"]), (False, "no_path", 300, "no way"))
        self.assertIs(ctx.LAST_VERB, report)

    def test_a_refused_path_fails_unless_straight_is_allowed(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = self.verb_ctx()
        ctx.resolve.return_value = 0x837
        ctx.my_position.return_value = [0, 0, 0]
        ctx.place_of.return_value = [500, 0, 0]
        ctx.is_actor.return_value = False
        ctx.path_leg.return_value = "refused"
        with self.assertRaises(TestFailure) as caught:
            PlayerVerbs.walk_to(ctx, "client", "lydia", 100.0, straight=False)
        self.assertEqual(caught.exception.outcome["why"], "no_path")
        ctx.steer_to.assert_not_called()
        ctx.player_script_call.assert_called_with("client", "Game", "SetPlayerAIDriven", [False])

    def test_a_menu_blocks_a_verb_by_name(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = self.verb_ctx()
        ctx.takes_input.return_value = False
        ctx.menus.return_value = {"HUD Menu", "Dialogue Menu"}
        with self.assertRaises(TestFailure) as caught:
            PlayerVerbs.can_play(ctx, "")
        self.assertEqual(caught.exception.outcome, {"why": "menu_blocked", "menus": ["Dialogue Menu"]})

    def test_talk_goes_on_with_the_open_conversation(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = self.verb_ctx()
        ctx.talking_to.return_value = True
        ctx.dialogue.return_value = {"options": [{"text": "a"}]}
        ctx.pick.return_value = {"picked": "a", "options": ["b"]}
        outcome = PlayerVerbs.talk(ctx, "", "faendal", ["a"])
        ctx.talk_to.assert_not_called()
        self.assertEqual((outcome["said"], outcome["options"]), (["a"], ["b"]))

    def test_use_fails_when_activating_changes_nothing(self):
        from coop_harness.actions import play_verbs
        ctx = self.verb_ctx()
        ctx.use_state.return_value = {"menus": set(), "sit": 0, "cell": 1, "object": {}}
        with patch.object(play_verbs, "USE_SECONDS", 0.05), self.assertRaises(TestFailure) as caught:
            play_verbs.PlayerVerbs.use(ctx, "", "chest")
        self.assertEqual(caught.exception.outcome["why"], "refused")

    def use_ctx(self, *states):
        ctx = self.verb_ctx()
        ctx.use_state.side_effect = [dict(zip(("menus", "sit", "cell", "object"), state)) for state in states]
        return ctx

    def test_use_on_a_chest_waits_past_its_lid_for_the_menu(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        chest, lid = {"items": {}}, {"items": {}, "lid": 1}
        ctx = self.use_ctx((set(), 0, 1, chest), (set(), 0, 1, lid), (set(), 0, 1, lid), ({"ContainerMenu"}, 0, 1, lid))
        outcome = PlayerVerbs.use(ctx, "", "chest")
        self.assertEqual((outcome["changed"], outcome["menus"]), (["menus", "object"], ["ContainerMenu"]))

    def test_use_on_a_chest_whose_menu_never_opens_is_refused(self):
        from coop_harness.actions import play_verbs
        ctx = self.verb_ctx()
        states = iter([{"menus": set(), "sit": 0, "cell": 1, "object": {"items": {}}}])
        lid = {"menus": set(), "sit": 0, "cell": 1, "object": {"items": {}, "lid": 1}}
        ctx.use_state.side_effect = lambda instance, ref: next(states, lid)
        with patch.object(play_verbs, "USE_SECONDS", 0.05), self.assertRaises(TestFailure) as caught:
            play_verbs.PlayerVerbs.use(ctx, "", "chest")
        self.assertEqual(caught.exception.outcome["why"], "refused")

    def test_use_on_a_door_is_done_when_only_the_door_moved(self):
        from coop_harness.actions import play_verbs
        ctx = self.verb_ctx()
        states = iter([{"menus": set(), "sit": 0, "cell": 1, "object": {"open": 0}}])
        swung = {"menus": set(), "sit": 0, "cell": 1, "object": {"open": 1}}
        ctx.use_state.side_effect = lambda instance, ref: next(states, swung)
        with patch.object(play_verbs, "USE_SECONDS", 0.05):
            self.assertEqual(play_verbs.PlayerVerbs.use(ctx, "", "door")["changed"], ["object"])

    def test_fight_swings_when_a_moving_target_stays_just_out_of_reach(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = self.verb_ctx()
        ctx.resolve.return_value = 0x837
        rows = iter([False, True, True])  # standing; dead after the swing, read by the loop and the check after it
        ctx.actor_row.side_effect = lambda instance, who: {"state": {"dead": who != 0x14 and next(rows)}}
        ctx.reach.side_effect = TestFailure("closest 125 units")
        self.assertEqual(PlayerVerbs.fight(ctx, "client", "wolf")["swings"], 1)
        ctx.attack.assert_called_once()

    def test_fight_stops_when_the_player_falls(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = self.verb_ctx()
        ctx.resolve.return_value = 0x837
        ctx.actor_row.side_effect = lambda instance, who: {"state": {"dead": who == 0x14}}
        with self.assertRaises(TestFailure) as caught:
            PlayerVerbs.fight(ctx, "", "wolf")
        self.assertEqual(caught.exception.outcome["why"], "player_down")
        ctx.attack.assert_not_called()


class Scenes(unittest.TestCase):
    def actor(self, name, session_id, x, role="own", player=False):
        return {"identity": {"name": name}, "mod": {"session_id": session_id}, "role": role, "player": player,
                "placement": {"position": [x, 0, 0]}}

    def test_handles_pair_the_games_by_session_and_number_shared_names(self):
        from coop_harness.actions import repro
        host = {"100": self.actor("Whiterun Guard", 1, 900), "101": self.actor("Whiterun Guard", 2, 100),
                "102": self.actor("Faendal", 3, 500), "103": self.actor("Alone", 4, 10),
                "20": self.actor("Hero", 5, 0, player=True)}
        guest = {"500": self.actor("Whiterun Guard", 1, 900, "copy"), "501": self.actor("Whiterun Guard", 2, 100, "copy"),
                 "102": self.actor("Faendal", 3, 500, "copy"), "20": self.actor("Friend", 6, 0, player=True)}
        named = repro.name_handles(repro.shared_actors(host, guest, [0, 0, 0]))
        self.assertEqual(list(named), ["whiterun_guard", "faendal", "whiterun_guard_2"])
        self.assertEqual((named["whiterun_guard"]["host"], named["whiterun_guard"]["guest"]), (101, 501))

    def test_a_handle_follows_its_copy_and_never_a_reused_id(self):
        from coop_harness.actions.repro import current_copies
        host = {"100": self.actor("Lydia", 7, 0), "20": self.actor("Hero", 1, 0, player=True)}
        wolf_on_the_old_id = {"500": self.actor("Wolf", 9, 0, "copy")}
        handles = {"lydia": {"host": 100, "guest": 500}, "host_player": {"host": 20, "guest": 300}, "chest": {"host": 1, "guest": 2}}
        fresh = current_copies(handles, host, wolf_on_the_old_id)
        self.assertEqual(fresh["lydia"], {"host": 100})  # gone from the guest: not the wolf
        self.assertEqual(fresh["host_player"], handles["host_player"])
        self.assertEqual(fresh["chest"], handles["chest"])
        back = {**wolf_on_the_old_id, "600": self.actor("Lydia", 7, 0, "copy")}
        self.assertEqual(current_copies(handles, host, back)["lydia"], {"host": 100, "guest": 600})

    def test_a_scene_shows_what_differs_at_its_start_and_only_new_differences_fail(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock()
        ctx.scene_start.return_value = {"guard level": "Whiterun Guard: level 1000 vs 1"}
        ctx.name_nearby.return_value = {}
        started = ReproActions.scene(ctx, at="coc Riverwood")
        self.assertEqual(started["disagree"], ["Whiterun Guard: level 1000 vs 1"])
        self.assertEqual(ctx.SCENE_KNOWN, {"guard level"})
        ctx.go_far.assert_called_with("", "coc Riverwood")
        ctx.pin_world.assert_called_once()

    def test_known_differences_are_left_out_of_the_lasting_ones(self):
        from coop_harness.observations import Observations
        ctx = Mock()
        ctx.find_desyncs.return_value = [("guard level", "old"), ("lydia hands", "new")]
        self.assertEqual(Observations.lasting_desyncs(ctx, samples=2, interval=0, known={"guard level"}), {"lydia hands": "new"})
        self.assertEqual(Observations.lasting_desyncs(ctx, samples=1, interval=0, known={"guard level", "lydia hands"}), {})

    def test_a_scene_is_remembered_for_a_recording_started_after_it(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock()
        ctx.scene_start.return_value = {}
        ctx.name_nearby.return_value = {"faendal": 300}
        self.assertEqual(ReproActions.scene(ctx, save="Late", at="coc Riverwood")["named"], {"faendal": 300})
        self.assertEqual(ctx.REPRO_PLACE, {"save": "Late", "at": "coc Riverwood", "pin": True})
        ctx.needs_save.assert_called_with("Late")

    def test_pinning_keeps_the_scale_to_put_back(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock(REPRO_PIN=None)
        ctx.calendar.return_value = {"scale": 20}
        ctx.game_state.return_value = {"weather": 0x81A}
        ReproActions.pin_world(ctx)
        ctx.console_on.assert_any_call("", "fw 81A")
        ctx.console_on.assert_called_with("", "set timescale to 0")
        ctx.calendar.return_value = {"scale": 0}
        ReproActions.pin_world(ctx)  # pinned twice: the scale before the first
        ReproActions.unpin_world(ctx)
        ctx.console_on.assert_called_with("", "set timescale to 20")
        self.assertIsNone(ctx.REPRO_PIN)

    def spot_ctx(self, floor, sight=None):
        ctx = Mock()
        ctx.failure.side_effect = lambda *args: TestFailure(*args)
        ctx.my_position.return_value = [75000.0, 0.0, 100.0]
        ctx.stage_spot.return_value = [0.0, 0.0, 0.0]
        ctx.player_snapshot.return_value = {"placement": {"rotation": [0, 0, 0.0]}}
        ctx.player_command.side_effect = [{"ray": floor}, {"ray": sight or {"hit": False}}]
        return ctx

    def test_stage_stands_ahead_of_the_host_when_the_party_left_home(self):
        from coop_harness.actions.repro import ReproActions
        ctx = self.spot_ctx({"hit": True, "point": [75000, 250, 90]})
        self.assertEqual(ReproActions.scene_spot(ctx), [75000.0, 250.0, 90])

    def test_stage_refuses_a_spot_with_no_floor_or_out_of_sight(self):
        from coop_harness.actions.repro import ReproActions
        with self.assertRaisesRegex(TestFailure, "no floor"):
            ReproActions.scene_spot(self.spot_ctx({"hit": False}))
        with self.assertRaisesRegex(TestFailure, "between"):
            ReproActions.scene_spot(self.spot_ctx({"hit": True, "point": [0, 0, 0]}, {"hit": True}))

    def test_stage_keeps_the_home_stage_at_home(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock()
        ctx.my_position.return_value = [100.0, 0.0, 0.0]
        ctx.stage_spot.return_value = [300.0, 0.0, 0.0]
        self.assertEqual(ReproActions.scene_spot(ctx), [300.0, 0.0, 0.0])
        ctx.player_command.assert_not_called()

    def test_observe_shows_only_what_changed_after_the_first_look(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock(spec=["handles", "read_handle", "refresh_handles"])
        ctx.handles.return_value = {"lydia": {}}
        host = {"placement.position": [0, 0, 0], "ai_state.package": 1, "ai.follow_target": 20, "state.dead": False}
        guest = {"placement.position": [30, 40, 0], "ai_state.package": 2, "ai.follow_target": 0, "state.dead": False}
        ctx.read_handle.return_value = {"host": host, "guest": guest}
        first = ReproActions.watch(ctx, ["lydia"])["lydia"]
        self.assertEqual(first["host"], host)
        self.assertEqual(first["apart"], 50)
        self.assertEqual(first["differs"], {"ai_state.package": [1, 2]})  # a copy's own AI and its place aren't compared
        ctx.read_handle.return_value = {"host": {**host, "state.dead": True}, "guest": guest}
        later = ReproActions.watch(ctx, ["lydia"])["lydia"]
        self.assertEqual((later["host"], later["guest"]), ({"state.dead": [False, True]}, {}))

    def test_observe_names_the_handles_it_knows(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock(spec=["handles", "failure", "refresh_handles"])
        ctx.handles.return_value = {"lydia": {}}
        ctx.failure.side_effect = lambda *args: TestFailure(*args)
        with self.assertRaisesRegex(TestFailure, "known: lydia"):
            ReproActions.watch(ctx, ["bandit"])

    def test_a_snapshot_reads_brief_unless_asked_for_all(self):
        from coop_harness.actions.repro import brief
        row = {"identity": {"name": "Lydia"}, "placement": {"position": [1.4, 2.6, 3.0]}, "graph": {"Speed": 1.0}}
        self.assertEqual(brief(row), {"identity.name": "Lydia", "placement.position": [1, 3, 3]})
        self.assertEqual(brief(row, full=True)["graph.Speed"], 1.0)

    def expect_ctx(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock()
        ctx.failure.side_effect = lambda *args: TestFailure(*args)
        ctx.expect_actor = lambda *args: ReproActions.expect_actor(ctx, *args)  # the real check, through ctx
        return ctx, ReproActions

    def test_expect_items_compares_what_both_games_hold(self):
        ctx, actions = self.expect_ctx()
        ctx.container_items.side_effect = [{0xF: 5}, {}]
        ctx.item_differences.return_value = "F: 5 (host) vs 0 (guest)"
        with self.assertRaisesRegex(TestFailure, "5 \\(host\\) vs 0"):
            actions.expect(ctx, "chest", "items")
        ctx.item_differences.assert_called_with({0xF: 5}, {}, "host", "guest")

    def test_expect_actor_reads_a_field_of_the_snapshot(self):
        ctx, actions = self.expect_ctx()
        ctx.actor_row.side_effect = lambda instance, ref: {"ai": {"follow_target": 0 if instance else 20}}
        self.assertEqual(actions.expect(ctx, "lydia", "actor", game="host", field="ai.follow_target", value="set"),
                         {"ai.follow_target": {"host": 20}})
        with self.assertRaisesRegex(TestFailure, "follow_target"):
            actions.expect(ctx, "lydia", "actor", game="guest", field="ai.follow_target", value="set")
        with self.assertRaisesRegex(TestFailure, "the same in both"):
            actions.expect(ctx, "lydia", "actor", field="ai.follow_target")

    def test_expect_agree_is_about_the_handle_unless_all(self):
        ctx, actions = self.expect_ctx()
        ctx.handles.return_value = {"lydia": {"host": 0xA2C8E}}
        ctx.check_desync.return_value = ["Lydia [host 000A2C8E, sid 3]: health", "Wolf [host 00000999, sid 4]: pose"]
        with self.assertRaisesRegex(TestFailure, "Lydia"):
            actions.expect(ctx, "lydia", "agree")
        ctx.handles.return_value = {"bandit": {"host": 0x111}}
        self.assertEqual(actions.expect(ctx, "bandit", "agree"), {"agree": True})
        with self.assertRaisesRegex(TestFailure, "Wolf"):
            actions.expect(ctx, "all", "agree")

    def test_expect_actor_waits_up_to_its_seconds(self):
        ctx, actions = self.expect_ctx()
        readings = iter([0, 0, 20])
        ctx.actor_row.side_effect = lambda instance, ref: {"ai": {"follow_target": next(readings)}}
        self.assertEqual(actions.expect(ctx, "lydia", "actor", game="guest", field="ai.follow_target", value="set", seconds=5),
                         {"ai.follow_target": {"guest": 20}})

    def test_an_expected_false_or_zero_survives_the_export(self):
        from coop_harness.actions import repro
        for value in (False, 0):
            entries = [{"tool": "expect", "ok": False,
                        "args": {"name": "wolf", "check": "actor", "game": "host", "field": "state.dead", "value": value}}]
            namespace = {}
            exec(compile(repro.export_scenario(entries, "x", "doc", []), "recorded", "exec"), namespace)
            ctx = Mock()
            namespace["run"](ctx)
            self.assertIs(ctx.expect.call_args.kwargs["value"], value)

    def test_a_reset_forgets_the_scene_for_the_next_recording(self):
        from coop_harness.actions.repro import ReproActions
        ctx = Mock(REPRO_PLACE={"at": "coc Riverwood"}, SCENE_KNOWN={"guard level"}, REPRO_SEEN={"lydia": {}})
        ReproActions.forget_scene(ctx)
        self.assertEqual((ctx.REPRO_PLACE, ctx.SCENE_KNOWN, ctx.REPRO_SEEN), (None, set(), {}))

    def test_a_goal_recording_exports_with_its_scene_and_a_spec_draft(self):
        from coop_harness.actions import repro
        entries = [{"tool": "scene", "args": {"save": "Late", "at": "coc Riverwood", "pin": True}, "ok": True},
                   {"tool": "stage", "args": {"spec": {"npcs": [{"name": "wolf", "base": "23ABE"}]}}, "ok": True},
                   {"tool": "go_to", "args": {"game": "host", "target": "wolf"}, "ok": True},
                   {"tool": "fight", "args": {"game": "host", "target": "wolf"}, "ok": False},
                   {"tool": "expect", "args": {"name": "all", "check": "agree"}, "ok": False}]
        code = repro.export_scenario(entries, "scene-repro", "doc", ["recorded"])
        namespace = {}
        exec(compile(code, "recorded", "exec"), namespace)
        self.assertIn("scene", namespace["TAGS"])
        ctx = Mock()
        namespace["run"](ctx)
        self.assertEqual([c[0] for c in ctx.method_calls], ["scene", "stage", "go_to", "expect", "unstage"])
        ctx.scene.assert_called_with(save="Late", at="coc Riverwood", pin=True)
        draft, leftovers = repro.spec_draft(entries, "scene-repro", "doc")
        self.assertEqual(draft["steps"], [{"do": "spawn_npc", "base": "23ABE", "as": "wolf"}, {"expect": "actors agree"}])
        self.assertEqual(len(leftovers), 2)  # the scene and the walk; a failed fight isn't replayed
        self.assertTrue(all(line.startswith("no spec step: ctx.") for line in leftovers))

    def test_a_scene_flow_is_run_only_by_name_and_alone(self):
        session = Session()
        self.addCleanup(session.close)
        session.registry.add("place", ["scene"], lambda session: None)
        self.assertEqual(session.registry["place"].fixture, "scene")
        self.assertNotIn("place", session.scenario_names([], None))
        self.assertEqual(session.scenario_names(["place"], None), ["place"])
        self.assertEqual(session.registry["faendal-letter"].fixture, "scene")


class Repro(unittest.TestCase):
    def test_a_recording_exports_as_a_scenario_keeping_its_failing_expect(self):
        from coop_harness.actions import repro
        entries = [
            {"tool": "duo_start", "args": {}, "ok": True},
            {"tool": "stage", "args": {"spec": {"objects": [{"name": "sword", "base": "12EB7"}]}}, "ok": True},
            {"tool": "world_freeze", "args": {"frozen": True}, "ok": True},
            {"tool": "press", "args": {"game": "guest", "key": "r", "seconds": 0.2}, "ok": True},
            {"tool": "console", "args": {"game": "host", "line": "bad"}, "ok": False},
            {"tool": "observe", "args": {"names": ["sword"]}, "ok": True},
            {"tool": "expect", "args": {"name": "sword", "check": "same_place", "within": 5.0}, "ok": False},
        ]
        code = repro.export_scenario(entries, "guest-drop-repro", "The guest's dropped sword.", ["recorded"])
        namespace = {}
        exec(compile(code, "recorded", "exec"), namespace)
        self.assertEqual(namespace["NAME"], "guest-drop-repro")
        ctx = Mock()
        namespace["run"](ctx)
        self.assertEqual([c[0] for c in ctx.method_calls],
                         ["stage", "freeze_world", "hold_key", "expect", "unstage"])
        ctx.hold_key.assert_called_with("client", "r", 0.2)
        self.assertIsNone(repro.export_spec(entries, "guest-drop-repro", "x", []))

    def test_a_recording_of_spec_steps_exports_as_a_spec(self):
        from coop_harness.actions import repro
        step = {"do": "add_items", "game": "host", "ref": "0xC2CE8", "item": "0x64B3D", "count": 2}
        entries = [{"tool": "spec_step", "args": {"step": step}, "ok": True},
                   {"tool": "observe", "args": {"names": []}, "ok": True}]
        spec = repro.export_spec(entries, "chest-repro", "doc", ["inventory"])
        self.assertEqual(spec["steps"], [step])

    def test_player_verbs_record_as_their_session_calls(self):
        from coop_harness.actions import repro
        entries = [{"tool": "go_to", "args": {"game": "guest", "target": "lydia", "within": 120.0}, "ok": True},
                   {"tool": "talk", "args": {"game": "guest", "target": "lydia", "say": ["follow me", "2"]}, "ok": True},
                   {"tool": "leave_dialogue", "args": {}, "ok": True},
                   {"tool": "menu", "args": {"game": "host", "name": "InventoryMenu", "show": True}, "ok": True}]
        namespace = {}
        exec(compile(repro.export_scenario(entries, "talk-repro", "doc", []), "recorded", "exec"), namespace)
        ctx = Mock()
        namespace["run"](ctx)
        self.assertEqual([c[0] for c in ctx.method_calls], ["go_to", "talk", "close_dialogues", "menu", "unstage"])
        ctx.go_to.assert_called_with("client", "lydia", 120.0, False, False)
        ctx.talk.assert_called_with("client", "lydia", ["follow me", 2])

    def test_walk_to_runs_straight_when_the_engine_finds_no_path(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = Mock()
        ctx.resolve.return_value = 0x837
        ctx.my_position.return_value = [0.0, 0.0, 0.0]
        ctx.place_of.return_value = [650.0, 0.0, 0.0]
        ctx.is_actor.return_value = True
        ctx.follow_run.return_value = "stalled"
        ctx.path_leg.return_value = "refused"
        outcome = PlayerVerbs.walk_to(ctx, "client", "lydia", 150.0)
        self.assertFalse(outcome["pathed"])
        ctx.steer_to.assert_called_with("client", "lydia", 150.0)
        ctx.player_script_call.assert_called_with("client", "Game", "SetPlayerAIDriven", [False])

    def test_a_recording_started_after_staging_starts_from_the_scene(self):
        from coop_harness.actions import repro
        session = Session()
        self.addCleanup(session.close)
        session.REPRO_SCENE = [{"npcs": [{"name": "lydia", "base": "0xA2C8E"}]}]
        session.booted_save = Mock(return_value=None)
        with tempfile.TemporaryDirectory() as folder, patch.object(repro, "RECORDINGS", Path(folder)):
            path = Path(session.record_start("walk"))
            session.record_call("walk_to", {"game": "guest", "target": "lydia"}, True)
            entries = [json.loads(line) for line in path.read_text().splitlines()]
        self.assertEqual([e["tool"] for e in entries], ["stage", "walk_to"])
        self.assertEqual(entries[0]["args"]["spec"]["npcs"][0]["name"], "lydia")

    def test_pick_waits_for_the_response(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = Mock()
        menus = iter([{"open": True, "current_info": 0, "options": [{"text": "a"}, {"text": "b"}]},
                      {"open": True, "current_info": 7, "options": [{"text": "a"}, {"text": "b"}]},
                      {"open": True, "current_info": 0, "options": [{"text": "a"}, {"text": "b"}]}])
        ctx.dialogue.side_effect = lambda instance: next(menus)
        ctx.player_command.return_value = {"ok": True}
        def wait_for(what, check, seconds):
            while not (result := check()):
                pass
            return result
        ctx.wait_for.side_effect = wait_for
        self.assertEqual(PlayerVerbs.pick(ctx, "", "b"), {"picked": "b", "options": ["a", "b"]})
        ctx.pick_line_with_keys.assert_called_with("", 1)  # real input, as a player

    def test_a_late_save_flow_exports_with_its_save_and_without_reads(self):
        from coop_harness.actions import repro
        entries = [{"tool": "needs_save", "args": {"save": "LateX"}, "ok": True},
                   {"tool": "travel", "args": {"game": "host", "command": "coc Riverwood"}, "ok": True},
                   {"tool": "console", "args": {"game": "host", "line": "getstage FreeformRiverwood01"}, "ok": True},
                   {"tool": "harness", "args": {"game": "host", "request": {"cmd": "dialogue"}}, "ok": True},
                   {"tool": "talk", "args": {"game": "host", "target": "faendal", "say": ["0"]}, "ok": True},
                   {"tool": "expect", "args": {"name": "FreeformRiverwood01", "check": "stage", "value": [10]}, "ok": True}]
        namespace = {}
        exec(compile(repro.export_scenario(entries, "letter", "doc", []), "recorded", "exec"), namespace)
        ctx = Mock()
        namespace["run"](ctx)
        self.assertEqual([c[0] for c in ctx.method_calls], ["needs_save", "go_far", "talk", "expect", "unstage"])
        ctx.talk.assert_called_with("", "faendal", [0])
        self.assertIn("late-save", namespace["TAGS"])
        ctx.expect.assert_called_with("FreeformRiverwood01", "stage", value=[10])

    def test_walk_to_walks_round_what_stops_a_run(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = Mock()
        ctx.resolve.return_value = 0x1348C
        places = iter([[0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [600.0, 0.0, 0.0], [990.0, 0.0, 0.0], [990.0, 0.0, 0.0]])
        ctx.my_position.side_effect = lambda instance: next(places)
        ctx.place_of.return_value = [1000.0, 0.0, 0.0]
        ctx.is_actor.return_value = True
        ctx.follow_run.side_effect = ["stalled", "arrived"]
        ctx.path_leg.return_value = "done"
        outcome = PlayerVerbs.walk_to(ctx, "", "faendal", 150.0)
        self.assertEqual((outcome["pathed"], outcome["runs"], outcome["legs"]), (True, 2, 1))
        ctx.steer_to.assert_not_called()

    def test_walk_to_paths_to_an_object(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = Mock()
        ctx.resolve.return_value = 0x5000
        places = iter([[0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [950.0, 0.0, 0.0], [950.0, 0.0, 0.0]])
        ctx.my_position.side_effect = lambda instance: next(places)
        ctx.place_of.return_value = [1000.0, 0.0, 0.0]
        ctx.is_actor.return_value = False
        ctx.path_leg.return_value = "arrived"
        self.assertEqual(PlayerVerbs.walk_to(ctx, "", "door", 100.0)["legs"], 1)
        ctx.follow_run.assert_not_called()

    def test_a_path_the_engine_ends_at_once_is_refused(self):
        from coop_harness.actions.play_verbs import PlayerVerbs
        ctx = Mock()
        ctx.my_position.return_value = [0.0, 0.0, 0.0]
        ctx.place_of.return_value = [650.0, 0.0, 0.0]
        ctx.player_command.side_effect = lambda instance, request: (
            {"ok": True, "id": 5} if request["cmd"] == "papyrus_call" else {"ok": True, "done": True, "value": False})
        self.assertEqual(PlayerVerbs.path_leg(ctx, "", 0x837, 150.0, time.monotonic() + 10), "refused")

    def test_a_walk_fails_in_seconds_not_minutes(self):
        from coop_harness.actions.play_verbs import travel_seconds, RUN_SPEED
        self.assertAlmostEqual(travel_seconds(2500, 250), 30.0)
        self.assertLess(travel_seconds(1000), 20.0)  # running: 1000 units take ~3 s
        self.assertEqual(travel_seconds(0), 10.0)
        self.assertGreater(RUN_SPEED, 300)

    def test_a_run_that_stops_moving_has_stalled(self):
        from coop_harness.actions.play_verbs import stalled
        moving = [(t / 10, [35.0 * t, 0.0, 0.0]) for t in range(30)]
        self.assertFalse(stalled(moving, 2.9))
        stuck = moving[:10] + [(t / 10, [315.0 + t * 0.5, 0.0, 0.0]) for t in range(10, 30)]
        self.assertTrue(stalled(stuck, 2.9))
        self.assertFalse(stalled(stuck[:5], 0.4))  # not under way long enough to tell

    def clock(self, module):
        """A controlled clock in that module's `time`: time.sleep moves it on."""
        now = [0.0]
        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)
        stack.enter_context(patch.object(module.time, "monotonic", lambda: now[0]))
        stack.enter_context(patch.object(module.time, "sleep", lambda seconds: now.__setitem__(0, now[0] + seconds)))
        return now

    def test_a_run_by_input_waits_out_its_ready_conditions(self):
        from coop_harness.actions import play_verbs as verbs
        now = self.clock(verbs)
        ctx = Mock()
        ctx.my_position.return_value = [0.0, 0.0, 0.0]
        ctx.place_of.return_value = [1000.0, 0.0, 0.0]
        ctx.act_start.return_value = 1
        ctx.player_command.side_effect = lambda instance, request: {"id": 1, "phase": "ready" if now[0] < 3 else "ended",
                                                                    "outcome": {"status": "done"}}
        self.assertTrue(verbs.PlayerVerbs.run_straight(ctx, "", 0x837, 100.0))

    def test_a_long_walk_to_an_object_gets_a_walk_s_time(self):
        from coop_harness.actions import play_verbs as verbs
        now = self.clock(verbs)
        ctx = Mock()
        ctx.resolve.return_value = 0x837
        ctx.is_actor.return_value = False
        ctx.my_position.side_effect = lambda instance: [now[0] * 80.0, 0.0, 0.0]
        ctx.place_of.return_value = [10000.0, 0.0, 0.0]
        def leg(*args):
            now[0] += 4.0
            return "done"
        ctx.path_leg.side_effect = leg
        self.assertTrue(verbs.PlayerVerbs.walk_to(ctx, "", "door", 100.0)["pathed"])

    def test_a_run_gets_its_time_to_set_off_before_standing_still_counts(self):
        from coop_harness.actions.play_verbs import set_off
        self.assertFalse(set_off([0, 0, 0], [0, 0, 0], 2.0))  # the engine has not started it yet
        self.assertTrue(set_off([0, 0, 0], [50, 0, 0], 2.0))
        self.assertTrue(set_off([0, 0, 0], [0, 0, 0], 6.0))

    def test_always_run_is_switched_on_for_a_walk_and_back_after(self):
        from coop_harness.actions.play_verbs import always_running
        ctx = Mock()
        states = iter([{"always_run": False}, {"always_run": True}])
        ctx.local_player.side_effect = lambda instance: next(states)
        ctx.wait_for.side_effect = lambda what, check, seconds: check()
        with always_running(ctx, "client"):
            self.assertEqual(ctx.toggle_always_run.call_count, 1)
        self.assertEqual(ctx.toggle_always_run.call_count, 2)
        ctx.reset_mock()
        ctx.local_player.side_effect = None
        ctx.local_player.return_value = {"always_run": True}
        with always_running(ctx, ""):
            pass
        ctx.toggle_always_run.assert_not_called()

    def test_handles_resolve_per_game(self):
        session = Session()
        self.addCleanup(session.close)
        session.name_handle("lydia", {"host": 0x812, "guest": 0x833})
        self.assertEqual(session.resolve("lydia", "host"), 0x812)
        self.assertEqual(session.resolve("lydia", "client"), 0x833)
        self.assertEqual(session.resolve("player", "guest"), 0x14)
        with self.assertRaises(TestFailure):
            session.resolve("nobody", "host")


CONTENDER = """
import sys
sys.path.insert(0, sys.argv[1])
from coop_harness.duo_lock import DuoLock
home, worktree, name, stops = sys.argv[2:6]
def stop(command, holder_worktree):
    open(stops, "a").write(holder_worktree + "\\n")
    return True
lock = DuoLock(home, worktree, name, stop=stop)
print("role", lock.acquire(name, timeout=60), flush=True)
sys.stdin.readline()
lock.release("done")
print("released", flush=True)
"""


@unittest.skipIf(WINDOWS, "the duo lock is Linux-only (fcntl)")
class DuoLockTests(unittest.TestCase):
    """The duo's lock (coop_harness/duo_lock.py) with contending processes in a temp home and fake games."""

    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.home = Path(folder.name)
        self.stops = self.home / "stops.txt"
        self.script = self.home / "contender.py"
        self.script.write_text(CONTENDER)

    def lock(self, worktree="/wt/main", name="main", **kwargs):
        from coop_harness.duo_lock import DuoLock
        lock = DuoLock(self.home, worktree, name, out=io.StringIO(), **kwargs)
        self.addCleanup(lock.release)
        return lock

    def contender(self, name):
        """Another agent's process wanting the duo, from its own worktree."""
        process = subprocess.Popen([sys.executable, str(self.script), str(Path(__file__).resolve().parent),
                                    str(self.home), f"/wt/{name}", name, str(self.stops)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.addCleanup(self.end, process)
        return process

    @staticmethod
    def end(process):
        if process.poll() is None:
            process.kill()
        process.wait()
        for pipe in filter(None, (process.stdin, process.stdout)):
            with contextlib.suppress(OSError):
                pipe.close()

    @staticmethod
    def line(process, timeout=10):
        """The process's next output line, or None if it printed none in time (it is still waiting)."""
        import select
        ready, _, _ = select.select([process.stdout], [], [], timeout)
        return process.stdout.readline().strip() if ready else None

    def until(self, condition, what):
        import time
        deadline = time.monotonic() + 10
        while not condition():
            if time.monotonic() > deadline:
                self.fail(f"never happened: {what}")
            time.sleep(0.02)

    def queue(self):
        return [entry["name"] for entry in self.lock().status()["queue"]]

    def holder(self):
        holder = self.lock().status()["holder"]
        return holder and holder["name"]

    def test_two_processes_contend_and_the_second_gets_it_when_the_first_releases(self):
        first = self.contender("first")
        self.assertEqual(self.line(first), "role holder")
        second = self.contender("second")
        self.until(lambda: self.queue() == ["second"], "second queued")
        self.assertIsNone(self.line(second, timeout=0.3))
        first.stdin.write("\n")
        first.stdin.flush()
        self.assertEqual(self.line(first), "released")
        self.assertEqual(self.line(second), "role holder")
        self.assertEqual(self.holder(), "second")
        self.assertFalse(self.stops.exists())  # a release, not a takeover: nothing to stop
        log = (self.home / "DUO.md").read_text()
        self.assertIn("first (/wt/first): first. done", log)
        self.assertIn("## Now\n\nsecond (worktree /wt/second)", log)

    def test_waiters_get_it_in_arrival_order(self):
        holder = self.contender("holder")
        self.assertEqual(self.line(holder), "role holder")
        waiters = []
        for name in ("b", "c", "d"):
            waiters.append(self.contender(name))
            self.until(lambda name=name: self.queue()[-1:] == [name], f"{name} queued")
        self.assertEqual(self.queue(), ["b", "c", "d"])
        previous = holder
        for waiter in waiters:
            previous.stdin.write("\n")
            previous.stdin.flush()
            self.assertEqual(self.line(waiter), "role holder")
            previous = waiter
        self.assertEqual(self.queue(), [])

    def test_a_killed_holder_frees_it_and_the_next_stops_its_games(self):
        victim = self.contender("victim")
        self.assertEqual(self.line(victim), "role holder")
        heir = self.contender("heir")
        self.until(lambda: self.queue() == ["heir"], "heir queued")
        victim.kill()
        self.assertEqual(self.line(heir), "role holder")
        self.assertEqual(self.stops.read_text(), "/wt/victim\n")
        self.assertIn("Stale (process ended); games stopped, taken over by heir", (self.home / "DUO.md").read_text())

    def test_a_dead_pid_in_the_state_is_taken_over_after_its_fake_game_is_stopped(self):
        import json
        import time
        game = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
        self.addCleanup(self.end, game)
        (self.home / "duo.json").write_text(json.dumps({"holder": {
            "id": "old", "name": "gone", "worktree": "/wt/gone", "pid": 999999, "since": time.time() - 9000,
            "what": "scenario x", "token": str(self.home / "duo-tokens/old.lock"), "stop": ["stop-gone"]}, "queue": []}))
        stopped = []

        def stop(command, worktree):
            stopped.append((command, worktree))
            game.kill()
            return game.wait() is not None

        lock = self.lock("/wt/new", "new", stop=stop)
        self.assertEqual(lock.acquire("a repro", wait=False), "holder")
        self.assertIsNotNone(game.poll())
        # The stale holder's checkout is gone: this checkout's stop path runs instead of its own.
        self.assertEqual(stopped, [(lock.stop_command, "/wt/gone")])
        self.assertEqual(self.holder(), "new")

    def test_a_stale_holder_whose_games_will_not_stop_is_not_taken_over(self):
        from coop_harness.duo_lock import DuoBusy
        victim = self.contender("victim")
        self.assertEqual(self.line(victim), "role holder")
        victim.kill()
        victim.wait()
        lock = self.lock("/wt/new", "new", stop=lambda command, worktree: False)
        with self.assertRaisesRegex(DuoBusy, "would not stop"):
            lock.acquire("x", wait=False)
        self.assertEqual(self.queue(), [])

    def test_the_same_checkout_joins_the_hold_and_others_wait(self):
        from coop_harness.duo_lock import DuoBusy
        holder = self.lock("/wt/a", "a")
        self.assertEqual(holder.acquire("duo --keep-running"), "holder")
        joiner = self.lock("/wt/a", "a")
        self.assertEqual(joiner.acquire("scenario x"), "joined")
        with self.assertRaisesRegex(DuoBusy, "held by a .*number 1 in the queue"):
            self.lock("/wt/b", "b").acquire("y", wait=False)
        self.assertEqual(self.queue(), [])  # no place kept without keep_place
        holder.release("done")
        self.assertEqual(self.holder(), None)

    def test_an_mcp_session_holds_it_across_calls_until_duo_stop(self):
        from coop_harness.actions import repro
        session = Session()
        self.addCleanup(session.close)
        session.DUO_LOCK = self.lock("/wt/mcp", "repro-mcp")
        self.assertEqual(session.duo_claim("repro-mcp", "repro session in QASmoke"), "holder")
        self.assertEqual(session.duo_claim("repro-mcp", "again"), "holder")  # a later call: still ours
        other = self.contender("other")
        self.until(lambda: self.queue() == ["other"], "other queued")
        with patch.object(Session, "stop_duo") as stop, patch.object(repro.ReproActions, "our_skyrims", return_value=[]):
            self.assertEqual(session.duo_free("repro-mcp", "done"), "freed")
        stop.assert_called_once()
        self.assertEqual(self.line(other), "role holder")
        with self.assertRaisesRegex(TestFailure, "not ours to stop"), patch.object(Session, "stop_duo") as stop:
            session.duo_free("repro-mcp", "again")
        stop.assert_not_called()

    def test_a_busy_mcp_start_keeps_its_place_for_the_next_call(self):
        first = self.contender("first")
        self.assertEqual(self.line(first), "role holder")
        session = Session()
        self.addCleanup(session.close)
        session.DUO_LOCK = self.lock("/wt/mcp", "repro-mcp")
        with self.assertRaisesRegex(TestFailure, "number 1 in the queue"):
            session.duo_claim("repro-mcp", "x", wait_seconds=0, keep_place=True)
        late = self.contender("late")
        self.until(lambda: self.queue() == ["repro-mcp", "late"], "late queued behind the session")
        first.stdin.write("\n")
        first.stdin.flush()
        self.assertEqual(session.duo_claim("repro-mcp", "x", wait_seconds=10, keep_place=True), "holder")
        self.assertIsNone(self.line(late, timeout=0.3))

    def test_a_hold_whose_games_run_goes_to_a_keeper_until_stop(self):
        keeper_script = self.home / "keeper.py"
        keeper_script.write_text(
            "import sys\nsys.path.insert(0, sys.argv[1])\nfrom coop_harness.duo_lock import DuoLock\n"
            "lock = DuoLock(sys.argv[2], '/wt/k', 'k')\n"
            "def ready():\n    print('ready', flush=True)\n"
            "lock.keep(sys.argv[3], lambda: True, check_seconds=0.05, ready=ready)\n")
        lock = self.lock("/wt/k", "k")
        lock.acquire("duo --keep-running")
        self.assertTrue(lock.park([sys.executable, str(keeper_script), str(Path(__file__).resolve().parent),
                                   str(self.home)]))
        self.assertIsNone(lock.role)
        status = self.lock().status()["holder"]
        self.assertTrue(status["keeper"] and status["alive"])
        other = self.contender("other")
        self.until(lambda: self.queue() == ["other"], "other queued")
        self.assertTrue(self.lock("/wt/k", "k").release_checkout("Stopped"))
        self.assertEqual(self.line(other), "role holder")

    def test_the_view_keeps_other_sections_and_puts_new_log_lines_first(self):
        (self.home / "DUO.md").write_text("# Duo\n\n## Rules\n\nold\n\n## Setup\n\nkeep me\n\n## Now\n\nfree\n\n"
                                          "## Waiting\n\n\n## Log\n\n- old line\n")
        lock = self.lock("/wt/a", "a")
        lock.acquire("x")
        lock.release("done")
        text = (self.home / "DUO.md").read_text()
        self.assertIn("keep me", text)
        self.assertIn("Claiming and freeing are automatic", text)
        self.assertLess(text.index("a (/wt/a): x. done"), text.index("old line"))


class KillMoveActions(unittest.TestCase):
    def test_a_failed_swing_lets_go_of_every_button_pressed(self):
        ctx = MagicMock()
        ctx.death_state.return_value = {}
        ctx.player_health.return_value = 100
        ctx.face.side_effect = TestFailure("no face")
        with patch("coop_harness.actions.combat.AnimTrace"):
            self.assertFalse(CombatActions.kill_move_exchange(ctx, {"": 1, "client": 2}))
        sent = [call.args for call in ctx.player_command.call_args_list]
        for game, button in (("", "forward"), ("client", "strafe_left")):
            self.assertIn((game, {"cmd": "input", "button": button, "down": False}), sent)

    def test_bare_hands_clear_a_spell_and_a_weapon_from_their_own_hands(self):
        ctx = MagicMock()
        hands = [{"state": {"hands": [0x12FCD, 0x12EB7]}}, {"state": {"hands": [0x12FCD, 0]}}]
        ctx.player_snapshot.side_effect = lambda instance: hands.pop(0) if len(hands) > 1 else hands[0]
        CombatActions.equip_weapon(ctx, "", 0)
        lines = [call.args[1] for call in ctx.console_on.call_args_list]
        self.assertIn("player.unequipspell 00012FCD left", lines)
        self.assertIn("player.unequipitem 00012EB7", lines)
        self.assertNotIn("player.unequipspell 00012EB7 right", lines)
        ctx.wait_for.assert_called_once()


class SkippedClipTests(unittest.TestCase):
    def trace(self, *states):
        from types import SimpleNamespace
        return SimpleNamespace(entries=[{"kind": "clips", "game": "client", "who": "lucan", "detail": {name: state}} for name, state in states])

    def test_a_clip_that_ends_without_its_file_is_named(self):
        from coop_harness.scenarios.dialogue import skipped_clips
        trace = self.trace(("Enter", {"loaded": False, "at_end": False}), ("Enter", {"loaded": False, "at_end": True}),
                           ("Idle", {"loaded": True, "at_end": False}))
        self.assertEqual(skipped_clips(trace), ["[client] lucan: Enter"])

    def test_a_clip_whose_file_arrives_in_time_is_not(self):
        from coop_harness.scenarios.dialogue import skipped_clips
        trace = self.trace(("Enter", {"loaded": False, "at_end": False}), ("Enter", {"loaded": True, "at_end": True}))
        self.assertEqual(skipped_clips(trace), [])

    def test_only_idles_and_gestures_start_a_clip_of_their_own(self):
        from coop_harness.scenarios.dialogue import is_clip_action
        def action(event, at=2.0):
            return {"kind": "action", "event": event, "time": at}
        self.assertTrue(is_clip_action(action("IdleHDLeftAngry"), 1.0))
        self.assertTrue(is_clip_action(action("DialogueNeutral"), 1.0))
        self.assertFalse(is_clip_action(action("IdleStopInstant"), 1.0))
        self.assertFalse(is_clip_action(action("moveStart"), 1.0))
        self.assertFalse(is_clip_action(action("IdleHDLeftAngry", 0.5), 1.0))


class ClipMismatchTests(unittest.TestCase):
    def trace(self, *rows):
        from types import SimpleNamespace
        return SimpleNamespace(entries=[{"kind": "clips", "game": game, "who": "lucan", "time": at, "clips": list(clips),
                                         "detail": {c: {"local_time": t, "duration": 3.0} for c, t in clips.items()}}
                                        for at, game, clips in rows])

    def test_the_same_clip_a_moment_later_matches(self):
        from coop_harness.scenarios.dialogue import clip_mismatches
        trace = self.trace((0.0, "", {"Idle": 1.0}), (0.1, "client", {"Idle": 1.0}), (5.0, "", {"Arms": 0.0}),
                           (5.3, "client", {"Arms": 0.0}), (9.0, "", {"Idle": 1.5}), (9.0, "client", {"Idle": 1.5}))
        self.assertEqual(clip_mismatches(trace), ([], 0))

    def test_an_action_playing_another_clip_is_named_on_both_sides(self):
        from coop_harness.scenarios.dialogue import clip_mismatches
        trace = self.trace((0.0, "", {"Idle": 1.0}), (0.0, "client", {"Idle": 1.0}), (2.0, "", {"NPC_TurnLeft180": 0.0}),
                           (2.2, "client", {"NPC_TurnLeft90": 0.0}), (9.0, "", {"Idle": 1.5}), (9.0, "client", {"Idle": 1.5}))
        self.assertEqual(clip_mismatches(trace)[0], ["lucan host NPC_TurnLeft180 at 2.0s", "lucan guest NPC_TurnLeft90 at 2.2s"])

    def test_the_counter_loop_running_on_its_own_clock_is_named(self):
        from coop_harness.scenarios.dialogue import clip_mismatches
        arms = "AnimObjectCounterRightToArm"
        trace = self.trace((0.0, "", {"Loop": 1.0}), (0.0, "client", {"Loop": 1.0}), (7.3, "client", {arms: 0.0}),
                           (26.7, "", {arms: 0.0}), (40.0, "", {"Loop": 1.5}), (40.0, "client", {"Loop": 1.5}))
        self.assertEqual(sorted(clip_mismatches(trace)[0]), [f"lucan guest {arms} at 7.3s", f"lucan host {arms} at 26.7s"])

    def test_a_final_gesture_is_compared_once_watched_past_it(self):
        from coop_harness.scenarios.dialogue import clip_mismatches
        rows = [(0.0, "", {"Idle": 1.0}), (0.1, "client", {"Idle": 1.0}), (2.0, "", {"ExpressiveE": 0.0}), (2.3, "client", {"ExpressiveH": 0.0})]
        trace = self.trace(*rows)
        self.assertEqual(clip_mismatches(trace)[0], [])  # not watched past it: require_clips_match fails on `through`
        for game in ("", "client"):  # places, each frame, no clip changing
            trace.entries.append({"kind": "pos", "game": game, "who": "lucan", "time": 5.0})
        self.assertEqual(clip_mismatches(trace)[0], ["lucan host ExpressiveE at 1.9s", "lucan guest ExpressiveH at 2.2s"])

    def test_a_trace_stopped_before_its_last_clip_is_compared_fails(self):
        from coop_harness.scenarios.dialogue import require_clips_match
        from types import SimpleNamespace
        trace = self.trace((0.0, "", {"Idle": 1.0}), (0.1, "client", {"Idle": 1.0}))
        ctx = SimpleNamespace(failure=lambda message: AssertionError(message))
        with self.assertRaisesRegex(AssertionError, "short of the last clip"):
            require_clips_match(ctx, trace, through=3.0)

    def test_clips_before_since_are_not_compared(self):
        from coop_harness.scenarios.dialogue import clip_mismatches
        trace = self.trace((0.0, "", {"Loop": 1.0}), (0.0, "client", {"Loop": 1.0}), (2.0, "client", {"Loop": 1.2, "Early": 0.0}),
                           (9.0, "", {"Loop": 1.5}), (9.0, "client", {"Loop": 1.5}))
        self.assertEqual(clip_mismatches(trace, since=5.0), ([], 0))

    def test_blended_clips_are_counted_not_compared(self):
        from coop_harness.scenarios.dialogue import clip_mismatches
        trace = self.trace((0.0, "", {"Idle": 1.0}), (0.0, "client", {"Idle": 1.0}), (2.0, "", {"Idle": 1.5, "MT_WalkForwardLeft": 0.0}),
                           (9.0, "", {"Idle": 1.5}), (9.0, "client", {"Idle": 1.5}))
        self.assertEqual(clip_mismatches(trace), ([], 1))

    def test_a_clip_cut_short_and_begun_again_is_a_start_a_loop_is_not(self):
        from coop_harness.scenarios.dialogue import clip_starts
        trace = self.trace((0.0, "", {"Turn": 1.0, "Idle": 2.9}), (0.5, "", {"Turn": 0.02, "Idle": 0.4}))
        self.assertEqual(clip_starts(trace, "", "lucan"), [(0.5, "Turn")])

    def test_a_restart_between_samples_shows_in_its_start_time(self):
        from coop_harness.scenarios.dialogue import clip_starts
        # Skyrim: 0.02 at both samples, 0.58 s apart: the host's MT_IdleHHDRightEnter begun again by a moveStop.
        trace = self.trace((11.24, "", {"Enter": 0.02}), (11.82, "", {"Enter": 0.02}))
        self.assertEqual(clip_starts(trace, "", "lucan"), [(11.82, "Enter")])


if __name__ == "__main__":
    unittest.main()
