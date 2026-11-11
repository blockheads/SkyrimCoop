"""Getting-up skeletons follow their owner even when Skyrim blends away from physics."""
import unittest

from coop_harness import Session
from coop_harness.objects import RagdollTrace
from coop_harness.errors import TestFailure


def trace(copy_bones, copy_bodies, owner_bones=None, hidden=()):
    result = RagdollTrace(Session(), "fixture", {"": {1: "owner"}, "client": {2: "copy"}})
    result.folder = type("Folder", (), {"name": "fixture"})()
    for game, actor, shift, bones, bodies in (("", 1, 0, owner_bones or [50] * 5, [0] * 5),
                                              ("client", 2, 300_000, copy_bones, copy_bodies)):
        result.samples[game] = [{"t": 1_000_000 + shift + i * 16_000, "id": actor, "knock": 6,
                                "head": [bone, 0, 30], "pelvis": [bone, 0, 0],
                                "flags_3d": (1 << 20) if game == "client" and i in hidden else 0,
                                "bodies": [{"bone": [bone, 0, 0], "position": [body, 0, 0],
                                            "motion": 2, "awake": False}]}
                               for i, (bone, body) in enumerate(zip(bones, bodies))]
    return result


class GetUpSkeleton(unittest.TestCase):
    def test_native_physics_offset_does_not_hide_matching_visible_bones(self):
        trace([50] * 5, [15] * 5).check_copy_skeleton("", 1, "client", 2)

    def test_bones_on_their_own_physics_still_must_follow_the_owner(self):
        with self.assertRaises(TestFailure):
            trace([90] * 5, [90] * 5).check_copy_skeleton("", 1, "client", 2)

    def test_copy_only_flickers_still_fail(self):
        with self.assertRaises(TestFailure):
            trace([50, 60, 50, 50, 50], [50, 60, 50, 50, 50]).check_copy_skeleton("", 1, "client", 2)

    def test_flickers_its_owner_has_too_are_skyrims(self):
        # knockdown-front-and-back, 2026-11-11: the host's own Lydia flickers 8 to 55 times a run, her copy as often.
        flicker = [50, 60, 50, 50, 50]
        trace(flicker, flicker, owner_bones=flicker).check_copy_skeleton("", 1, "client", 2)

    def test_a_delay_anywhere_in_its_range_follows_the_owner(self):
        # The copy went down sooner once knocked by its owner's action: 160 ms behind, not the old fixed 300.
        result = trace([50] * 5, [50] * 5)
        for frame in result.samples["client"]:
            frame["t"] -= 140_000
        result.check_copy_skeleton("", 1, "client", 2)

    def test_bones_nobody_sees_are_not_compared(self):
        # Pushed out of the guest's view, Skyrim leaves the culled skeleton where it was.
        trace([50, 50, 90, 90, 50], [50, 50, 90, 90, 50], hidden=(2, 3)).check_copy_skeleton("", 1, "client", 2)


def snapping(owner_at, copy_at, owner_jump=20, copy_jump=20):
    """Both stand still; each one's bone jumps that far for one frame at those frames."""
    result = RagdollTrace(Session(), "fixture", {"": {1: "owner"}, "client": {2: "copy"}})
    for game, actor, shift, at, jump in (("", 1, 0, owner_at, owner_jump), ("client", 2, 200_000, copy_at, copy_jump)):
        result.samples[game] = [{"t": 1_000_000 + shift + i * 16_000, "id": actor, "knock": 6, "position": [0, 0, 0],
                                "head": [0, 0, 30], "pelvis": [0, 0, 0],
                                "bodies": [{"bone": [jump if i in at else 0, 0, 0], "position": [0, 0, 0], "motion": 2,
                                            "awake": True}]} for i in range(80)]
    return result


class GetUpSnaps(unittest.TestCase):
    def test_a_snap_its_owner_never_has_is_the_copys(self):
        self.assertEqual(len(snapping((), (30,)).copy_snaps("", 1, "client", 2)), 1)

    def test_the_tail_of_its_owners_shake_is_not_the_copys(self):
        # knockdown-smoke, 2026-11-11: the owner's limb shook for 170 ms; its copy's last snaps came after the owner's.
        self.assertEqual(snapping((20, 22, 24, 26), (22, 24, 26, 28, 34, 36)).copy_snaps("", 1, "client", 2), [])

    def test_a_shake_just_under_the_snap_size_on_its_owner_is_still_its_owners(self):
        # The owner's bone went back and forth 11 units a frame, its copy's 13: one each side of SNAP.
        self.assertEqual(snapping((20, 22, 24), (22, 24, 26), owner_jump=11, copy_jump=13).copy_snaps("", 1, "client", 2), [])

    def test_a_snap_twice_its_owners_is_the_copys(self):
        self.assertEqual(len(snapping((20,), (22,), owner_jump=13, copy_jump=40).copy_snaps("", 1, "client", 2)), 1)

    def test_a_snap_long_after_its_owners_is_the_copys(self):
        self.assertEqual(len(snapping((10,), (60,)).copy_snaps("", 1, "client", 2)), 1)


if __name__ == "__main__":
    unittest.main()
