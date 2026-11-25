"""The duo's comparison must catch state differences without mistaking inventory delta bookkeeping for contents."""
import math
import unittest
import copy

from coop_harness.common import log_time
from coop_harness import smoothness
from coop_harness.common import FORM_TYPE_NAMES
from coop_harness.comparisons import compare_actors, dialogue_differences, reference_differences, refused_replays, world_differences


EXPL_TYPE = next(k for k, v in FORM_TYPE_NAMES.items() if v == "EXPL")


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

    def test_the_hosts_leveled_roll_with_the_same_items_is_not_a_difference(self):
        # End comparisons in towns: 0x8000000 (16128) on the host's containers only, the guest given its items.
        self.assertEqual(reference_differences({"items": {15: 3}, "changes": 0x8000020},
                                              {"items": {15: 3}, "changes": 0x20}), {})

    def test_the_leveled_roll_with_other_items_is_still_reported(self):
        differences = reference_differences({"items": {15: 3}, "changes": 0x8000020}, {"items": {15: 2}, "changes": 0x20})
        self.assertIn("items", differences)
        self.assertIn("changes", differences)


class LooseClutter(unittest.TestCase):
    """Where a placed physics object lies is each game's own (piece-f-plan.md, F2): bugs.md's ladles."""
    LADLE = {"id": 0x32AFE, "base": 0x319E5, "type": 32, "cell": 0x32AE7, "flags": 0, "position": [2623.0, 1985.0, 7043.0],
             "rotation": [0.0, 0.0, 1.0],
             "collision": [{"node": "WoodenLadle01", "in_world": True, "motion": 2, "position": [2623.0, 1985.0, 7043.0],
                            "rotation": [0.0, 0.0, 0.0, 1.0]}]}

    def test_a_ladle_turned_and_moved_on_one_game_is_not_a_difference(self):
        body = {**self.LADLE["collision"][0], "position": [2628.0, 1985.0, 7043.0], "rotation": [0.0, 0.0, 0.15, 0.99]}
        moved = {**self.LADLE, "position": [2628.0, 1985.0, 7043.0], "rotation": [0.0, 0.0, 1.3], "changes": 0x2,
                 "collision": [body]}
        self.assertEqual(reference_differences(self.LADLE, moved), {})

    def test_a_ladle_whose_body_left_the_world_on_one_game_is_still_reported(self):
        body = {**self.LADLE["collision"][0], "in_world": False}
        self.assertIn("collision", reference_differences(self.LADLE, {**self.LADLE, "collision": [body]}))

    def test_a_ladle_with_another_body_on_one_game_is_still_reported(self):
        extra = {**self.LADLE["collision"][0], "node": "WoodenLadle01:1"}
        self.assertIn("collision", reference_differences(self.LADLE, {**self.LADLE, "collision": [self.LADLE["collision"][0], extra]}))

    def test_a_ladle_taken_on_one_game_is_still_reported(self):
        self.assertIn("flags", reference_differences(self.LADLE, {**self.LADLE, "flags": 0x800}))

    def test_a_ladle_on_one_game_only_is_still_reported(self):
        self.assertEqual(len(world_differences({self.LADLE["id"]: self.LADLE}, {})), 1)

    def test_a_created_items_place_is_still_compared(self):
        dropped = {**self.LADLE, "id": 0xFF000901}
        self.assertIn("position", reference_differences(dropped, {**dropped, "position": [2700.0, 1985.0, 7043.0]}))

    def test_a_keyframed_objects_place_is_still_compared(self):
        door = {**self.LADLE, "collision": [{**self.LADLE["collision"][0], "motion": 4}]}
        self.assertIn("position", reference_differences(door, {**door, "position": [2700.0, 1985.0, 7043.0]}))


class Scenery(unittest.TestCase):
    """Critters, fish, their markers and plants' sway are each game's own (one-simulator.md, piece 10); state stays
    compared. References from bugs.md's Riverwood, plains and rejoin runs."""
    MOTH = {"id": 0xFF000A01, "base": 0xB97AF, "type": 24, "cell": 0x3C, "flags": 0, "position": [100.0, 200.0, 300.0],
            "scripts": ["Critter"], "on_path": True, "changes": 0x80000000}
    SALMON = {"id": 0x10C3BA, "base": 0x10C3BA, "type": 24, "cell": 0x9732, "flags": 0, "position": [10.0, 20.0, 30.0],
              "scripts": ["FXfakeCritterScript"]}
    MARKER = {"id": 0xFF000A02, "base": 0xC2D47, "type": 32, "cell": 0x3C, "flags": 0, "position": [5.0, 5.0, 5.0]}
    PHEASANT = {"id": 0x38BE6, "base": 0x38B0C, "type": 39, "cell": 0x133C6, "flags": 0, "changes": 0x2, "position": [1.0, 1.0, 1.0],
                "pose": {"b_Lower": {"position": [1.0, 1.0, 1.0], "rotate": [1.0] + [0.0] * 8}}}
    CABBAGE = {"id": 0x6A0BB, "base": 0x64B3F, "type": 32, "cell": 0x133C6, "flags": 0, "position": [0.0, 0.0, 0.0]}

    def test_a_moth_on_one_game_only_is_not_a_difference(self):
        self.assertEqual(world_differences({self.MOTH["id"]: self.MOTH}, {}), {})

    def test_a_critters_marker_on_one_game_only_is_not_a_difference(self):
        self.assertEqual(world_differences({}, {self.MARKER["id"]: self.MARKER}), {})

    def test_a_fish_elsewhere_is_not_a_difference(self):
        moved = {**self.SALMON, "position": [500.0, 20.0, 30.0]}
        self.assertEqual(world_differences({self.SALMON["id"]: self.SALMON}, {moved["id"]: moved}), {})

    def test_a_fish_whose_script_only_one_game_reads_is_still_left_out(self):
        bare = {key: value for key, value in self.SALMON.items() if key != "scripts"}
        moved = {**bare, "position": [500.0, 20.0, 30.0]}
        self.assertEqual(world_differences({self.SALMON["id"]: self.SALMON}, {moved["id"]: moved}), {})

    def test_a_plants_sway_is_not_a_difference(self):
        turned = {**self.PHEASANT, "pose": {"b_Lower": {"position": [1.0, 1.0, 1.0], "rotate": [0.0, 1.0] + [0.0] * 7}}}
        self.assertEqual(world_differences({self.PHEASANT["id"]: self.PHEASANT}, {turned["id"]: turned}), {})

    def test_a_hanging_plants_bodies_swung_differently_are_not_a_difference(self):
        # guest-rejoins-while-joined, 2026-11-21: Lucan's pheasants, b_Upper and b_Lower within a unit, turned.
        body = {"node": "b_Upper", "in_world": True, "motion": 2, "position": [-340.0, -2.0, 167.0], "rotation": [0.0, 0.0, 0.0, 1.0]}
        swung = {**body, "position": [-339.0, -2.0, 168.0], "rotation": [0.0, 0.3, 0.0, 0.95]}
        host, guest = {**self.PHEASANT, "collision": [body]}, {**self.PHEASANT, "collision": [swung]}
        self.assertEqual(world_differences({host["id"]: host}, {guest["id"]: guest}), {})
        moved = {**body, "position": [-300.0, -2.0, 167.0]}
        self.assertIn("collision", next(iter(world_differences({host["id"]: host}, {guest["id"]: {**guest, "collision": [moved]}}).values())))

    def test_a_harvested_plant_is_still_a_difference(self):
        picked = {**self.PHEASANT, "flags": 0x2000}
        self.assertIn("flags", next(iter(world_differences({self.PHEASANT["id"]: self.PHEASANT}, {picked["id"]: picked}).values())))

    def test_a_moved_cabbage_is_still_a_difference(self):
        moved = {**self.CABBAGE, "position": [40.0, 0.0, 0.0]}
        self.assertIn("position", next(iter(world_differences({self.CABBAGE["id"]: self.CABBAGE}, {moved["id"]: moved}).values())))

    def test_a_created_item_on_one_game_is_reported_whatever_the_other_game_has_under_its_id(self):
        # Review of PR #91: created ids are each game's own; a host moth at the guest item's id hid it.
        item = {"id": 0xFF000A01, "base": 0x64B3F, "type": 32, "cell": 0x3C, "flags": 0, "position": [1.0, 2.0, 3.0]}
        self.assertEqual(list(world_differences({self.MOTH["id"]: self.MOTH}, {item["id"]: item}).values()), [{"present": "client only"}])

    def test_an_explosions_debris_is_not_a_difference(self):
        # spike_wall_hurts_both, 2026-11-23: a created explosion's AddOnNode43 pieces 100 units apart.
        boom = {"id": 0xFF00089E, "base": 0x88109, "type": EXPL_TYPE, "cell": 0x3C, "flags": 0, "changes": 2, "position": [4928.0, 992.0, -224.0],
                "pose": {"AddOnNode43": {"position": [4758.0, 879.0, -443.0], "rotate": [1.0] + [0.0] * 8}}}
        flown = {**boom, "id": 0xFF000901, "pose": {"AddOnNode43": {"position": [4928.0, 992.0, -224.0], "rotate": [1.0] + [0.0] * 8}}}
        # darts_hurt_puller, 2026-11-24: its debris spheres' bodies apart too.
        sphere = {"node": "cSphere01", "in_world": True, "motion": 2, "position": [4644.0, 1472.0, -495.0], "rotation": [0.0, 0.0, 0.0, 1.0]}
        boom["collision"] = [sphere]
        flown["collision"] = [{**sphere, "position": [4608.0, 1460.0, -496.0], "rotation": [0.0, 0.3, 0.0, 0.95]}]
        self.assertEqual(world_differences({boom["id"]: boom}, {flown["id"]: flown}), {})
        elsewhere = {**flown, "position": [5928.0, 992.0, -224.0]}
        self.assertNotEqual(world_differences({boom["id"]: boom}, {elsewhere["id"]: elsewhere}), {})

    def test_a_cabbage_on_one_game_only_is_still_a_difference(self):
        self.assertEqual(list(world_differences({self.CABBAGE["id"]: self.CABBAGE}, {}).values()), [{"present": "host only"}])


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

    def test_a_culled_skeleton_is_stale_though_was_in_frustum_says_seen(self):
        # kWasInFrustum is refreshed only by ProcessTracking; the 3D's kNotVisible is the frame's truth.
        worlds = self.worlds()
        worlds["host"]["100"]["mod"]["pose_visibility"]["drawn"] = False
        self.assertEqual(compare_actors(worlds), [])
        worlds["host"]["100"]["mod"]["pose_visibility"]["drawn"] = True
        self.assertTrue(any("pose.head" in key for key, _ in compare_actors(worlds)))

    def test_culled_copy_skeleton_is_a_stale_observation(self):
        self.assertEqual(compare_actors(self.worlds(copy_visible=False)), [])

    def test_visible_pose_mismatch_is_still_reported(self):
        self.assertTrue(any("pose.head" in key for key, _ in compare_actors(self.worlds())))

    def test_animation_is_still_checked_when_bones_are_culled(self):
        worlds = self.worlds(owner_visible=False)
        worlds["client"]["200"]["animation"]["idle"] = 0
        self.assertEqual([key.rsplit(" ", 1)[-1] for key, _ in compare_actors(worlds)], ["animation.idle"])

    def test_a_players_copy_not_drawn_is_a_stale_observation(self):
        # Skyrim (duo, teleport-keeps-copies, 2026-11-10): the guest's player's copy read head 81 for 3.7 s after a
        # landing, culled on the host, while its graph matched its owner's on every update (524 compared).
        self.assertEqual(compare_actors(self.worlds(copy_visible=False, player=True)), [])

    def test_a_players_body_is_checked_whenever_its_copy_is_drawn(self):
        # Its own game reads its body as it stands, in first person too: only the copy's side can be stale.
        self.assertTrue(any("pose.head" in key for key, _ in compare_actors(self.worlds(False, True, player=True))))
        self.assertTrue(any("pose.head" in key for key, _ in compare_actors(self.worlds(player=True))))


class Smoothness(unittest.TestCase):
    """coop_harness/smoothness.py: speeds per engine frame, so uneven frames moving evenly in time read as smooth."""

    @staticmethod
    def entries(who, moves, frames):
        x, out, t = 0.0, [], 0.0
        for f, (move, dt) in enumerate(zip(moves, frames)):
            out.append({"kind": "pos", "game": "", "who": who, "f": f, "time": t, "dt": dt * 1000, "pos": [x, 0.0, 0.0], "yaw": 0.0})
            x += move
            t += dt
        return out

    def test_even_motion_over_uneven_frames_is_smooth(self):
        frames = [0.010, 0.024] * 20
        path = smoothness.series(self.entries("a", [300 * dt for dt in frames], frames), "", "a")
        self.assertEqual(smoothness.roughness(smoothness.frame_speeds(path, lag=1))["median"], 0.0)

    def test_a_step_off_by_a_third_every_other_frame_is_rough(self):
        frames = [1 / 60] * 40
        moves = [5.0 * (1.33 if i % 2 else 1.0) for i in range(40)]
        found = smoothness.roughness(smoothness.frame_speeds(smoothness.series(self.entries("a", moves, frames), "", "a")))
        self.assertGreater(found["median"], 0.1)

    def test_a_copy_behind_its_owner_tracks_at_its_delay(self):
        frames = [1 / 60] * 120
        owner = smoothness.series(self.entries("owner", [5.0] * 120, frames), "", "owner")
        copy = [(f, t + 0.1, dt, p, y) for f, t, dt, p, y in owner]
        found = smoothness.tracking(owner, copy)
        self.assertEqual(found["delay_ms"], 100)
        self.assertLess(found["mean"], 0.5)

    def test_message_bytes_per_second(self):
        perf = {"messages": {"window_s": 2.0, "top": [{"name": "ReferencesMoveRequest", "way": "o", "count": 60, "bytes": 7200}]}}
        self.assertEqual(smoothness.message_bytes(perf)["ReferencesMoveRequest o"], {"bytes_s": 3600, "count_s": 30.0, "bytes_each": 120.0})

    @staticmethod
    def posed(frames, feet_at, yaw_at=lambda f: 0.0):
        """A drawn actor running along x, its left foot at feet_at(frame) ahead of its root (its heading's forward)."""
        rows = []
        for f in range(frames):
            x, yaw = 5.0 * f, yaw_at(f)
            ahead = lambda units: [x + units * math.sin(yaw), units * math.cos(yaw), 0.0]
            rows.append({"f": f, "time": f / 60, "dt": 16.7, "pos": [x, 0.0, 0.0], "yaw": yaw, "root": [x, 0.0, 0.0],
                         "cull": [True, 7, True], "feet": [ahead(feet_at(f)), ahead(0.0)]})
        return rows

    def test_frozen_feet_count_as_held_however_long(self):
        self.assertEqual(smoothness.pose_steps(self.posed(300, lambda f: 3.0 * f))["held"], 0.0)
        found = smoothness.pose_steps(self.posed(840, lambda f: 3.0 * min(f, 240)))
        self.assertGreater(found["held"], 0.7)

    def test_a_turn_alone_moves_no_feet(self):
        found = smoothness.pose_steps(self.posed(120, lambda f: 20.0, lambda f: 0.1 * f))
        self.assertEqual(found["held"], 1.0)


if __name__ == "__main__":
    unittest.main()
