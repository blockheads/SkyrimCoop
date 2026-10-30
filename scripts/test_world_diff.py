"""The duo's comparison must catch state differences without mistaking inventory delta bookkeeping for contents."""
import unittest

from coop_harness.common import log_time
from coop_harness.comparisons import dialogue_differences, reference_differences, refused_replays


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


if __name__ == "__main__":
    unittest.main()
