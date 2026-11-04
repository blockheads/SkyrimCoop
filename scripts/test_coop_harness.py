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
from unittest.mock import Mock, patch

from coop_harness import Session
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
        entries = [{"tool": "walk_to", "args": {"game": "guest", "target": "lydia", "within": 120.0}, "ok": True},
                   {"tool": "talk_to", "args": {"game": "guest", "target": "lydia"}, "ok": True},
                   {"tool": "pick", "args": {"game": "guest", "line": "follow me"}, "ok": True},
                   {"tool": "leave_dialogue", "args": {}, "ok": True},
                   {"tool": "menu", "args": {"game": "host", "name": "InventoryMenu", "show": True}, "ok": True}]
        namespace = {}
        exec(compile(repro.export_scenario(entries, "talk-repro", "doc", []), "recorded", "exec"), namespace)
        ctx = Mock()
        namespace["run"](ctx)
        self.assertEqual([c[0] for c in ctx.method_calls], ["walk_to", "talk_to", "pick", "close_dialogues", "menu", "unstage"])
        ctx.walk_to.assert_called_with("client", "lydia", 120.0)

    def test_walk_to_runs_straight_when_the_engine_finds_no_path(self):
        from coop_harness.actions.repro import PlayerVerbs
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
        from coop_harness.actions.repro import PlayerVerbs
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

    def test_a_late_save_flow_exports_with_its_save_and_without_reads(self):
        from coop_harness.actions import repro
        entries = [{"tool": "needs_save", "args": {"save": "LateX"}, "ok": True},
                   {"tool": "travel", "args": {"game": "host", "command": "coc Riverwood"}, "ok": True},
                   {"tool": "console", "args": {"game": "host", "line": "getstage FreeformRiverwood01"}, "ok": True},
                   {"tool": "harness", "args": {"game": "host", "request": {"cmd": "dialogue"}}, "ok": True},
                   {"tool": "pick", "args": {"game": "host", "line": "0"}, "ok": True},
                   {"tool": "expect", "args": {"name": "FreeformRiverwood01", "check": "stage", "value": [10]}, "ok": True}]
        namespace = {}
        exec(compile(repro.export_scenario(entries, "letter", "doc", []), "recorded", "exec"), namespace)
        ctx = Mock()
        namespace["run"](ctx)
        self.assertEqual([c[0] for c in ctx.method_calls], ["needs_save", "go_far", "pick", "expect", "unstage"])
        ctx.pick.assert_called_with("", 0)
        self.assertIn("late-save", namespace["TAGS"])
        ctx.expect.assert_called_with("FreeformRiverwood01", "stage", value=[10])

    def test_walk_to_walks_round_what_stops_a_run(self):
        from coop_harness.actions.repro import PlayerVerbs
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
        from coop_harness.actions.repro import PlayerVerbs
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
        from coop_harness.actions.repro import PlayerVerbs
        ctx = Mock()
        ctx.my_position.return_value = [0.0, 0.0, 0.0]
        ctx.place_of.return_value = [650.0, 0.0, 0.0]
        ctx.player_command.side_effect = lambda instance, request: (
            {"ok": True, "id": 5} if request["cmd"] == "papyrus_call" else {"ok": True, "done": True, "value": False})
        self.assertEqual(PlayerVerbs.path_leg(ctx, "", 0x837, 150.0, time.monotonic() + 10), "refused")

    def test_a_walk_fails_in_seconds_not_minutes(self):
        from coop_harness.actions.repro import travel_seconds, RUN_SPEED
        self.assertAlmostEqual(travel_seconds(2500, 250), 30.0)
        self.assertLess(travel_seconds(1000), 20.0)  # running: 1000 units take ~3 s
        self.assertEqual(travel_seconds(0), 10.0)
        self.assertGreater(RUN_SPEED, 300)

    def test_a_run_that_stops_moving_has_stalled(self):
        from coop_harness.actions.repro import stalled
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
        from coop_harness.actions import repro as verbs
        now = self.clock(verbs)
        ctx = Mock()
        ctx.my_position.return_value = [0.0, 0.0, 0.0]
        ctx.place_of.return_value = [1000.0, 0.0, 0.0]
        ctx.act_start.return_value = 1
        ctx.player_command.side_effect = lambda instance, request: {"id": 1, "phase": "ready" if now[0] < 3 else "ended",
                                                                    "outcome": {"status": "done"}}
        self.assertTrue(verbs.PlayerVerbs.run_straight(ctx, "", 0x837, 100.0))

    def test_a_long_walk_to_an_object_gets_a_walk_s_time(self):
        from coop_harness.actions import repro as verbs
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
        from coop_harness.actions.repro import set_off
        self.assertFalse(set_off([0, 0, 0], [0, 0, 0], 2.0))  # the engine has not started it yet
        self.assertTrue(set_off([0, 0, 0], [50, 0, 0], 2.0))
        self.assertTrue(set_off([0, 0, 0], [0, 0, 0], 6.0))

    def test_always_run_is_switched_on_for_a_walk_and_back_after(self):
        from coop_harness.actions.repro import always_running
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


if __name__ == "__main__":
    unittest.main()
