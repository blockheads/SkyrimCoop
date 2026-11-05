"""The IDA hooks (scripts/re/ida_hook.py): calls become journal rows, the wrong database is refused, reminders come
when RE falls behind and only then. Payload shapes are the ones Claude Code and ida-multi-mcp send."""
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "re"))
import ida_hook  # noqa: E402
import journal  # noqa: E402

MOVE = "0x140689370"
DECOMPILED = json.dumps({"addr": MOVE, "code": "// vfunc 0xC8\n_DWORD *__fastcall sub_140689370(\n  __int64 a1)\n{"})


class FakeCheckout(ida_hook.Checkout):
    def __init__(self, root, database, instances=None):
        self.root, self.version, self.branch = Path(root), "1.7.104", "agent/test"
        self.journal = journal.file_for(self.root, self.version, self.branch)
        self.database = Path(database)
        self._instances = instances or {}

    def ids(self):
        return {0x689370: 37507}

    def instances(self):
        return self._instances


class HookTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.root = Path(self.folder.name)
        (self.root / "build").mkdir()
        self.database = self.root / "shared.i64"
        self.checkout = FakeCheckout(self.root, self.database, {"good": str(self.database), "bad": "/other/x.i64"})

    def tearDown(self):
        self.folder.cleanup()

    def post(self, tool, arguments, response="", session="s1"):
        out = io.StringIO()
        with redirect_stdout(out):
            ida_hook.post(self.checkout, {"tool_name": tool, "tool_input": arguments, "tool_response": response,
                                          "session_id": session})
        text = out.getvalue()
        return json.loads(text)["hookSpecificOutput"]["additionalContext"] if text else None

    def read(self, address, name):
        return self.post("mcp__ida-multi-mcp__decompile", {"addr": address, "instance_id": "good"},
                         json.dumps({"addr": address, "code": f"void __fastcall {name}(__int64 a1)\n{{"}))


class Rows(HookTest):
    def test_a_read_journals_the_function_and_its_name_once(self):
        self.post("mcp__ida-multi-mcp__decompile", {"addr": MOVE, "instance_id": "good"}, DECOMPILED)
        self.post("mcp__ida-multi-mcp__decompile", {"addr": MOVE, "instance_id": "good"}, DECOMPILED)
        rows = journal.load(self.checkout.journal)
        self.assertEqual([(row.op, row.id, row.rva, row.item) for row in rows],
                         [("read", 37507, 0x689370, {"name": "sub_140689370"})])

    def test_disasm_reports_its_name(self):
        response = json.dumps({"addr": MOVE, "asm": {"name": "Actor::Move", "start_ea": MOVE, "lines": "..."}})
        self.post("mcp__ida-multi-mcp__disasm", {"addr": MOVE, "instance_id": "good"}, response)
        self.assertEqual(journal.load(self.checkout.journal)[0].item, {"name": "Actor::Move"})

    def test_a_failed_read_or_a_name_for_an_address_is_not_journaled(self):
        self.post("mcp__ida-multi-mcp__decompile", {"addr": "Actor::Move", "instance_id": "good"},
                  json.dumps({"addr": "Actor::Move", "code": None, "error": "Failed to parse address: Actor::Move"}))
        self.assertEqual(journal.load(self.checkout.journal), [])

    def test_renames_keep_only_what_succeeded(self):
        arguments = {"batch": {"func": [{"addr": MOVE, "name": "Actor::Move"}, {"addr": "0x1", "name": "X"}],
                               "local": {"func_addr": MOVE, "old": "v14", "new": "pProcess"}}, "instance_id": "good"}
        response = json.dumps({"func": [{"addr": MOVE, "ok": True, "error": None}, {"addr": "0x1", "error": "no"}],
                               "local": [{"ok": True}]})
        self.post("mcp__ida-multi-mcp__rename", arguments, response)
        rows = journal.load(self.checkout.journal)
        self.assertEqual([(row.op, row.rva, row.item) for row in rows],
                         [("name", 0x689370, {"addr": MOVE, "name": "Actor::Move"}),
                          ("local", 0x689370, {"func_addr": MOVE, "old": "v14", "new": "pProcess"})])

    def test_comments_types_and_declarations(self):
        comment = "Sit state 0:\tsteps\nelse places"
        self.post("mcp__ida-multi-mcp__set_comments", {"items": {"addr": MOVE, "comment": comment}},
                  json.dumps([{"addr": MOVE, "ok": True, "error": "Failed to set decompiler comment"}]))
        self.post("mcp__ida-multi-mcp__set_type", {"edits": [{"addr": MOVE, "signature": "void f(Actor*)"}]},
                  json.dumps([{"edit": {}, "ok": True}]))
        self.post("mcp__ida-multi-mcp__declare_type", {"decls": "struct Foo { int a; };"},
                  json.dumps([{"decl": "struct Foo { int a; };", "ok": True}]))
        rows = journal.load(self.checkout.journal)
        self.assertEqual([row.op for row in rows], ["comment", "type", "decl"])
        self.assertEqual(rows[0].item["comment"], comment)  # tabs and newlines survive the TSV
        self.assertEqual((rows[2].rva, rows[2].item), (None, {"decl": "struct Foo { int a; };"}))

    def test_reads_of_other_tools_are_not_rows(self):
        self.post("mcp__ida-multi-mcp__xrefs_to", {"addrs": MOVE, "instance_id": "good"}, "[]")
        self.assertFalse(self.checkout.journal.exists())


class Database(HookTest):
    def pre(self, instance):
        error = io.StringIO()
        with redirect_stderr(error):
            code = ida_hook.pre(self.checkout, {"tool_input": {"instance_id": instance}})
        return code, error.getvalue()

    def test_the_shared_database_and_unknown_instances_pass(self):
        self.assertEqual(self.pre("good")[0], 0)
        self.assertEqual(self.pre("gone")[0], 0)
        self.assertEqual(self.pre(None)[0], 0)

    def test_another_database_is_refused_naming_the_right_instance(self):
        code, message = self.pre("bad")
        self.assertEqual(code, 2)
        self.assertIn("/other/x.i64", message)
        self.assertIn("use instance_id good", message)


class Reminders(HookTest):
    def test_every_third_undocumented_read_reminds_and_documenting_pays_it_down(self):
        self.assertIsNone(self.read("0x140000010", "sub_140000010"))
        self.assertIsNone(self.read("0x140000020", "Actor::Named"))  # already named: no debt
        self.assertIsNone(self.read("0x140000030", "sub_140000030"))
        message = self.read("0x140000040", "sub_140000040")
        self.assertIn("3 functions read", message)
        self.assertIn("sub_140000040", message)
        self.post("mcp__ida-multi-mcp__set_comments", {"items": {"addr": "0x140000010", "comment": "not on the path"}},
                  json.dumps([{"ok": True}]))
        self.assertNotIn("sub_140000010", self.read("0x140000050", "sub_140000050"))  # 3 again, without the commented
        self.assertIsNone(self.read("0x140000060", "sub_140000060"))

    def test_the_duo_reminds_once_without_reads(self):
        message = self.post("mcp__skyrim-coop__duo_start", {"cell": "QASmoke"})
        self.assertIn("No IDA reads", message)
        self.assertIn("instance_id good", message)
        self.assertIsNone(self.post("Bash", {"command": "python3 scripts/coop_test.py duo --cell X"}))
        self.assertIn("No IDA reads", self.post("Bash", {"command": "scripts/coop_test.py scenario x"}, session="s2"))

    def test_the_duo_after_reads_and_other_commands_say_nothing(self):
        self.assertIsNone(self.post("Bash", {"command": "scripts/check.sh"}))
        self.read(MOVE, "Actor::Move")
        self.assertIsNone(self.post("mcp__skyrim-coop__duo_start", {}))

    def test_engine_code_edits_remind_once_without_reads(self):
        harness = self.root / "Code/client/TestHarness/Commands.cpp"
        self.assertIsNone(self.post("Edit", {"file_path": str(harness)}))
        self.assertIn("engine-facing", self.post("Edit", {"file_path": str(self.root / "Code/client/Systems/A.h")}))
        self.assertIsNone(self.post("Write", {"file_path": str(self.root / "Code/client/Games/B.h")}))

    def test_py_eval_writes_are_pointed_at_the_journaled_tools(self):
        self.assertIn("py_eval", self.post("mcp__ida-multi-mcp__py_eval", {"code": "idc.set_name(ea, 'X')"}))
        self.assertIsNone(self.post("mcp__ida-multi-mcp__py_eval", {"code": "print(idaapi.get_func_name(ea))"}))


class Start(HookTest):
    def test_names_the_instance_and_the_branch_so_far(self):
        self.read(MOVE, "Actor::Move")
        out = io.StringIO()
        with redirect_stdout(out):
            ida_hook.start(self.checkout, {})
        self.assertIn("pass instance_id good", out.getvalue())
        self.assertIn("1 functions read", out.getvalue())


if __name__ == "__main__":
    unittest.main()
