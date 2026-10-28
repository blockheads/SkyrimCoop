"""Getting-up skeletons follow their owner even when Skyrim blends away from physics."""
import unittest

from coop_test import RagdollTrace, TestFailure


def trace(copy_bones, copy_bodies):
    result = RagdollTrace("fixture", {"": {1: "owner"}, "client": {2: "copy"}})
    result.folder = type("Folder", (), {"name": "fixture"})()
    for game, actor, shift, bones, bodies in (("", 1, 0, [50] * 5, [0] * 5),
                                              ("client", 2, 300_000, copy_bones, copy_bodies)):
        result.samples[game] = [{"t": 1_000_000 + shift + i * 16_000, "id": actor, "knock": 6,
                                "head": [bone, 0, 30], "pelvis": [bone, 0, 0],
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


if __name__ == "__main__":
    unittest.main()
