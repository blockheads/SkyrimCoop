"""scripts/check_re.py: added notes and journal rows name real IDs at their RVAs; systems docs use names, not sub_."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path[:0] = [str(Path(__file__).resolve().parent), str(Path(__file__).resolve().parent / "re")]
import check_re  # noqa: E402
import journal  # noqa: E402

LIBRARY = {"1.7.104": {37507: 0x689370}}
NOTES = "docs/re/ida_notes_1.7.104.tsv"
JOURNAL = "docs/re/journal/1.7.104/agent-x.tsv"


class Problems(unittest.TestCase):
    def check(self, path, *lines, libraries=LIBRARY):
        return check_re.problems({path: list(lines)}, libraries)

    def test_a_good_note_and_journal_row_pass(self):
        self.assertEqual(self.check(NOTES, "id\trva\tname\tcomment", "37507\t0x689370\tActor::Move\tsteps"), [])
        row = '2026-11-05T10:00:00Z\tname\t37507\t0x689370\t{"addr": "0x140689370", "name": "Actor::Move"}'
        self.assertEqual(self.check(JOURNAL, "time\top\tid\trva\titem", row), [])

    def test_a_row_without_a_valid_id_fails(self):
        self.assertIn("not a number", self.check(NOTES, "\t0x689370\tActor::Move\tsteps")[0])
        self.assertIn("not in the Address Library", self.check(NOTES, "99999999\t0x689370\tX\t")[0])
        self.assertIn("is at 0x689370, not 0x689371", self.check(NOTES, "37507\t0x689371\tX\t")[0])
        self.assertIn("is at", self.check(JOURNAL, '2026\tcomment\t37507\t0x1\t{}')[0])

    def test_journal_rows_without_an_id_pass(self):
        self.assertEqual(self.check(JOURNAL, '2026\tdecl\t\t\t{"decl": "struct A;"}'), [])

    def test_ids_go_unchecked_without_the_library(self):
        self.assertEqual(self.check(NOTES, "99999999\t0x1\tX\t", libraries={"1.7.104": None}), [])

    def test_auto_names_fail_in_notes_and_systems_docs_not_elsewhere(self):
        found = self.check("docs/re/systems/actor-movement.md", "stored in the process (sub_1407023E0)")
        self.assertIn("sub_1407023E0 is IDA's auto name", found[0])
        self.assertEqual(self.check("docs/re/systems/a.md", "Actor::Move (37507), substate_1"), [])
        self.assertEqual(self.check("docs/bugs.md", "sub_1407023E0"), [])
        self.assertEqual(self.check(JOURNAL, '2026\tread\t37507\t0x689370\t{"name": "sub_140689370"}'), [])

    def test_pr_18s_notes(self):
        """The furniture fix's actual additions: the IDs hold up, the auto name in a comment is caught."""
        rows = ["37507\t0x689370\tActor::Move\tvfunc 0xC8", "37996\t0x6b1ab0\tBSTEventSink::Handle\t"
                "stored in the process (sub_1407023E0) for ApplyMovement"]
        found = self.check(NOTES, *rows, libraries={"1.7.104": {37507: 0x689370, 37996: 0x6b1ab0}})
        self.assertEqual(len(found), 1)
        self.assertIn("sub_1407023E0", found[0])


class ReferenceData(unittest.TestCase):
    def test_journal_names_and_comments_reach_the_engine_reference(self):
        import build_re_data
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            journal.append(journal.file_for(root, "1.7.104", "agent/x"), [
                journal.Row("2026-11-05T10:00:00Z", "name", 37507, 0x689370, {"name": "Actor::Move"}),
                journal.Row("2026-11-05T10:01:00Z", "comment", 37507, 0x689370, {"comment": "old"}),
                journal.Row("2026-11-05T10:02:00Z", "comment", 37507, 0x689370, {"comment": "Steps:\tthen\nplaces\n[re]\nx"}),
                journal.Row("2026-11-05T10:03:00Z", "read", 37996, 0x6b1ab0, {"name": "sub_1406B1AB0"})])
            saved = build_re_data.ROOT, build_re_data.IDA_NOTES, build_re_data.TARGET_NOTES, build_re_data.EXE_VERSION
            try:
                build_re_data.ROOT, build_re_data.EXE_VERSION = root, "1.7.104.0"
                build_re_data.IDA_NOTES = build_re_data.TARGET_NOTES = root / "none.tsv"
                notes = build_re_data.load_ida_notes()
            finally:
                build_re_data.ROOT, build_re_data.IDA_NOTES, build_re_data.TARGET_NOTES, build_re_data.EXE_VERSION = saved
        self.assertEqual(notes, {37507: ("Actor::Move", "Steps: then / places")})


if __name__ == "__main__":
    unittest.main()
