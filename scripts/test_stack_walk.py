"""No stack capture in the plugin goes around CrashHandler::CaptureStack (docs/re/systems/stack-walk.md)."""
import unittest
from pathlib import Path

CLIENT = Path(__file__).resolve().parent.parent / "Code" / "client"
# The harness's stack_walk_check runs the system's walk on request, to measure it.
ALLOWED = {"TestHarness/CommandsMemory.cpp"}
SYSTEM_WALKS = ("RtlCaptureStackBackTrace", "CaptureStackBackTrace", "RtlWalkFrameChain", "StackWalk64")


class StackWalk(unittest.TestCase):
    def test_every_capture_uses_the_guarded_walk(self):
        found = []
        for path in sorted(CLIENT.rglob("*")):
            if path.suffix not in (".cpp", ".h") or path.relative_to(CLIENT).as_posix() in ALLOWED:
                continue
            for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if any(name + "(" in line for name in SYSTEM_WALKS):
                    found.append(f"{path.relative_to(CLIENT)}:{number}")
        self.assertEqual(found, [], "use CrashHandler::CaptureStack: the system's walk faults under SKSE's stubs")

    def test_the_tools_capture_through_it(self):
        for name in ("HeapHistory.cpp", "Games/Skyrim/BSCore/LockOrder.cpp", "TestHarness/ActivateWatch.cpp",
                     "TestHarness/AnimationTrace.cpp", "skse_entry.cpp"):
            self.assertIn("CrashHandler::CaptureStack(", (CLIENT / name).read_text(errors="replace"), name)


if __name__ == "__main__":
    unittest.main()
