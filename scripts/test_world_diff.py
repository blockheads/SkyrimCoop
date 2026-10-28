"""The duo's comparison must catch state differences without mistaking inventory delta bookkeeping for contents."""
import unittest

from coop_test import reference_differences


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


if __name__ == "__main__":
    unittest.main()
