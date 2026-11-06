"""The duo's comparison must catch state differences without mistaking inventory delta bookkeeping for contents."""
import unittest
import copy

from coop_harness.common import log_time
from coop_harness.comparisons import compare_actors, dialogue_differences, reference_differences, refused_replays, world_differences


class ContainerComparison(unittest.TestCase):
    def test_equal_contents_with_different_inventory_delta_records(self):
        self.assertEqual(reference_differences({"items": {15: 3}, "changes": 0x40},
                                              {"items": {15: 3}, "changes": 0x60}), {})

    def test_inventory_difference_is_still_reported(self):
        differences = reference_differences({"items": {15: 3}, "changes": 0x40},
                                            {"items": {15: 4}, "changes": 0x60})
        self.assertIn("items", differences)

    def test_other_record_rows_are_still_reported(self):
        self.assertIn("changes", reference_differences({"items": {}, "changes": 0x40},
                                                       {"items": {}, "changes": 0x80}))

    def test_inventory_flags_without_a_contents_snapshot_are_still_reported(self):
        self.assertIn("changes", reference_differences({"changes": 0x40}, {"changes": 0x60}))


class DeathDropSword(unittest.TestCase):
    """A death drop and its twin, the same sword at rest (measured 2026-11-05 on the host's death, before players kept theirs)."""
    HOST = {"id": 0xFF00087D, "base": 0x12EB7, "type": 41, "cell": 0x32AE7, "flags": 0x100008, "owner": 7, "changes": 0x446,
            "position": [-183.0, 2022.0, 7049.0], "position_3d": [-183.0, 2022.0, 7049.0], "rotation": [1.81, -0.32, 1.97],
            "pose": {"Weapon  (00012EB7)": {"position": [-178.0, 2028.0, 6978.0], "rotate": [1.0] * 9}},
            "collision": [{"node": "Weapon  (00012EB7)", "in_world": True, "motion": 3, "position": [-178.0, 2028.0, 6978.0],
                           "rotation": [0.6, -0.8, 0.0, 0.0]}]}
    GUEST = {"id": 0xFF000837, "base": 0x12EB7, "type": 41, "cell": 0x32AE7, "flags": 0x8, "owner": 7, "changes": 0x46,
             "position": [-178.0, 2028.0, 6978.0], "position_3d": [-178.0, 2028.0, 6978.0], "rotation": [-3.1, 0.0, -1.8],
             "pose": {"IronLongSword01": {"position": [-178.0, 2028.0, 6978.0], "rotate": [1.0] * 9}},
             "collision": [{"node": "IronLongSword01", "in_world": True, "motion": 3, "position": [-178.0, 2028.0, 6978.0],
                            "rotation": [0.6, -0.8, 0.0, 0.0]}]}

    def differences(self, host=None, guest=None):
        host, guest = {**self.HOST, **(host or {})}, {**self.GUEST, **(guest or {})}
        return world_differences({host["id"]: host}, {guest["id"]: guest})

    def test_one_sword_at_one_place_agrees(self):
        self.assertEqual(self.differences(), {})

    def test_a_missing_owner_is_reported(self):
        differences = self.differences(guest={"owner": None})
        self.assertEqual([list(fields) for fields in differences.values()], [["owner"]])

    def test_a_body_elsewhere_is_two_swords(self):
        body = {**self.GUEST["collision"][0], "position": [-100.0, 2028.0, 6978.0]}
        self.assertEqual(len(self.differences(host={"collision": [body]})), 2)

    def test_a_second_sword_is_reported(self):
        second = {**self.GUEST, "id": 0xFF000838, "position": [0.0, 0.0, 0.0], "collision": []}
        differences = world_differences({self.HOST["id"]: self.HOST}, {self.GUEST["id"]: self.GUEST, second["id"]: second})
        self.assertEqual([fields for fields in differences.values()], [{"present": "client only"}])


class RefusedReplays(unittest.TestCase):
    LINES = ["[2026-10-30 09:32:29.656] [debug] Copy 846DE refused replayed action 959FE 'turnStop' (weapon state 0)",
             "[2026-10-30 09:32:29.784] [debug] Copy 846DE refused replayed action 959FE 'turnStop' (weapon state 0)",
             "[2026-10-30 09:32:30.001] [debug] Copy FF000847 refused replayed action 489F1 '' (weapon state 0)",
             "[2026-10-30 09:32:30.100] [debug] Copy FF000847 took replayed action 489F1 'DialogueGeneric' 2 graph updates late",
             "not a log line"]

    def test_counts_each_copy_and_animation_since_the_scenario_began(self):
        self.assertEqual(refused_replays(self.LINES, log_time("2026-10-30 09:32:29.700")),
                         {(0x846DE, "turnStop"): 1, (0xFF000847, "489F1"): 1})


class DialogueDifferences(unittest.TestCase):
    A = [[0x10, 0x20, "I could help you get the claw back."], [0x11, 0x21, "Wait, you mean this golden claw?"]]
    B = [[0x12, 0x22, "Goodbye."]]

    @staticmethod
    def entry(t, options, speaker=7, **fields):
        return {"t": int(t * 1e6), "menu": True, "speaker": speaker, "shown": True, "options": options, "cursor": 0,
                "info": 0, **fields}

    def test_a_guest_trailing_by_less_than_the_grace_agrees(self):
        host = [self.entry(0, self.A), self.entry(5, self.B)]
        guest = [self.entry(0.2, self.A), self.entry(5.5, self.B)]
        self.assertEqual(dialogue_differences(host, guest, int(10e6)), [])

    def test_a_guest_list_a_step_behind_is_reported_once(self):
        host = [self.entry(0, self.A), self.entry(5, self.B)]
        guest = [self.entry(0.2, self.A)]
        found = dialogue_differences(host, guest, int(10e6))
        self.assertEqual([(field, round(at, 1)) for at, field, _, _ in found], [("options", 5.0)])

    def test_each_players_own_line_instead_of_the_partys_list_is_reported(self):
        found = dialogue_differences([self.entry(0, self.A[:1])], [self.entry(0, self.A[1:])], int(3e6))
        self.assertEqual([field for _, field, _, _ in found], ["options"])

    def test_hidden_lists_are_compared_only_by_being_hidden(self):
        host = [self.entry(0, self.A, shown=False)]
        guest = [self.entry(0, self.B, shown=False)]
        self.assertEqual(dialogue_differences(host, guest, int(5e6)), [])
        found = dialogue_differences(host, [self.entry(0, self.A)], int(5e6))
        self.assertEqual([field for _, field, _, _ in found], ["shown"])

    def test_two_conversations_are_not_compared(self):
        self.assertEqual(dialogue_differences([self.entry(0, self.A, speaker=7)], [self.entry(0, self.B, speaker=8)],
                                              int(5e6)), [])


class PoseVisibility(unittest.TestCase):
    def worlds(self, owner_visible=True, copy_visible=True, player=False):
        actor = {"role": "own", "player": player, "identity": {"name": "Lydia"},
                 "animation": {"idle": 0xA8524},
                 "pose": {"head": 88.0, "pelvis": 70.0, "floor_gap": 0.0, "skeleton": 0.0},
                 "mod": {"session_id": 3, "components": ["Local"],
                         "pose_visibility": {"was_in_frustum": owner_visible}}}
        duplicate = copy.deepcopy(actor)
        duplicate["role"] = "copy"
        duplicate["pose"]["head"] = 122.0
        duplicate["mod"]["components"] = ["Remote", "Interpolation"]
        duplicate["mod"]["pose_visibility"]["was_in_frustum"] = copy_visible
        return {"host": {"100": actor}, "client": {"200": duplicate}}

    def test_culled_owner_skeleton_is_a_stale_observation(self):
        self.assertEqual(compare_actors(self.worlds(owner_visible=False)), [])

    def test_npc_names_use_the_pools_case_insensitive_equality(self):
        worlds = self.worlds(owner_visible=False)
        worlds["client"]["200"]["identity"]["name"] = "lydia"
        self.assertEqual(compare_actors(worlds), [])

    def test_different_resolved_npc_names_are_still_reported(self):
        worlds = self.worlds(owner_visible=False)
        worlds["host"]["100"]["identity"]["name"] = "Bandit Chief"
        worlds["client"]["200"]["identity"]["name"] = "bandit outlaw"
        self.assertTrue(any("identity.name" in key for key, _ in compare_actors(worlds)))

    def test_culled_copy_skeleton_is_a_stale_observation(self):
        self.assertEqual(compare_actors(self.worlds(copy_visible=False)), [])

    def test_visible_pose_mismatch_is_still_reported(self):
        self.assertTrue(any("pose.head" in key for key, _ in compare_actors(self.worlds())))

    def test_animation_is_still_checked_when_bones_are_culled(self):
        worlds = self.worlds(owner_visible=False)
        worlds["client"]["200"]["animation"]["idle"] = 0
        self.assertEqual([key.rsplit(" ", 1)[-1] for key, _ in compare_actors(worlds)], ["animation.idle"])

    def test_player_body_checks_keep_their_existing_scope(self):
        self.assertTrue(any("pose.head" in key for key, _ in compare_actors(self.worlds(False, False, player=True))))


if __name__ == "__main__":
    unittest.main()
