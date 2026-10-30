"""Late-save fixtures retain the story's weather and actors while detecting new leftovers."""
import contextlib
import json
import unittest
from unittest.mock import Mock, patch

import coop_test as ct


class LateBaseline(unittest.TestCase):
    def fixture(self):
        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)
        self.time = dict(year=201, month=7, day=18, hour=19.5, scale=20, raw_days=16.8, days_passed=16.8)
        self.player = dict(placement=dict(position=[10, 20, 30], rotation=[0, 0, 1], cell=44),
                           equipment=[], values={key: 100 for key in
                                                 ('health', 'magicka', 'stamina', 'max_health',
                                                  'max_magicka', 'max_stamina')}, state={})
        self.home = dict(position=[10, 20, 30], heading=1, equipment=[], level=20, cell=44,
                         values=dict(health=100, magicka=100, stamina=100),
                         world=dict(weather=0x10A244, time=self.time), npcs=[0x1A67F], copies=[0x1A67F])
        functions = dict(calendar=Mock(return_value=self.time),
                         game_state=Mock(return_value=dict(online=True, level=20,
                                                           weather=0x10A244, time=self.time)),
                         player_snapshot=Mock(return_value=self.player),
                         owned_npcs=Mock(return_value=[0x1A67F]),
                         remote_npcs=Mock(return_value={0x1A67F}),
                         actors_on=Mock(return_value={}), other_player=Mock(return_value=1),
                         load_home=Mock(return_value=self.home), player_cell=Mock(return_value=44),
                         dialogue=Mock(return_value={}), reset_player=Mock(), reset_world=Mock(),
                         console_on=Mock(), check_desync=Mock(return_value=[]),
                         atomic_write=Mock(), log=Mock())
        for name, function in functions.items():
            stack.enter_context(patch.object(ct, name, function))
        return functions

    def test_saved_world_keeps_its_quest_weather_time_and_npcs(self):
        functions = self.fixture()
        ct.save_home(cell_name=None)
        for call in functions['atomic_write'].call_args_list:
            home = json.loads(call.args[1])
            self.assertEqual(home['world']['weather'], 0x10A244)
            self.assertEqual(home['world']['time']['hour'], 19.5)
            self.assertEqual(home.get('npcs'), [0x1A67F])
            self.assertEqual(home.get('copies'), [0x1A67F])

    def test_qasmoke_still_uses_clear_noon_without_story_actors(self):
        functions = self.fixture()
        ct.save_home()
        for call in functions['atomic_write'].call_args_list:
            home = json.loads(call.args[1])
            self.assertEqual(home['world']['weather'], ct.CLEAR_WEATHER)
            self.assertEqual(home['world']['time']['hour'], 12)
            self.assertEqual(home.get('npcs', []), [])

    def test_cleanup_removes_new_test_actors_and_keeps_saved_story_actors(self):
        functions = self.fixture()
        functions['owned_npcs'].side_effect = lambda name: [0x1A67F, 0xFF000800] if not name else []
        ct.reset_baseline()
        commands = functions['console_on'].call_args_list
        self.assertEqual([call.args for call in commands],
                         [('', 'FF000800.disable'), ('', 'FF000800.markfordelete')])

    def test_saved_story_actors_are_part_of_the_initial_baseline(self):
        self.fixture()
        self.assertEqual(ct.check_baseline(), [])

    def test_new_actors_and_copies_still_fail_the_baseline(self):
        functions = self.fixture()
        self.home['world']['live'] = True
        functions['owned_npcs'].return_value = [0x1A67F, 0xFF000800]
        functions['remote_npcs'].return_value = [0x1A67F, 0xFF000801]
        problems = ct.check_baseline()
        self.assertTrue(any('FF000800' in problem for problem in problems))
        self.assertTrue(any('FF000801' in problem for problem in problems))
        self.assertFalse(any('0001A67F' in problem for problem in problems))

    def test_story_weather_and_time_can_advance_together(self):
        functions = self.fixture()
        self.home['world']['live'] = True
        functions['game_state'].return_value.update(weather=0x10A245, time={**self.time, 'hour': 20})
        self.assertEqual(ct.check_baseline(), [])

    def test_story_weather_disagreement_still_fails(self):
        functions = self.fixture()
        self.home['world']['live'] = True
        functions['game_state'].side_effect = lambda name: dict(online=True, level=20, time=self.time,
                                                              weather=0x10A244 if not name else 0x10A245)
        self.assertTrue(any('weather' in problem for problem in ct.check_baseline()))

    def test_story_reset_does_not_force_weather_over_a_running_quest(self):
        reset_world = ct.reset_world
        functions = self.fixture()
        self.home['world']['live'] = True
        reset_world(self.home)
        functions['console_on'].assert_not_called()

    def test_late_arriving_placed_story_actor_is_not_deleted(self):
        functions = self.fixture()
        self.home['world']['live'] = True
        self.home['npcs'] = []
        self.home['copies'] = []
        ct.reset_baseline()
        functions['console_on'].assert_not_called()
        self.assertEqual(ct.check_baseline(), [])


if __name__ == '__main__':
    unittest.main()
