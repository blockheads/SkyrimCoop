import importlib.util
import json
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("hot_reload", Path(__file__).resolve().parents[1] / "scripts/hot_reload.py")
reload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reload)


class ReloadCommands(unittest.TestCase):
    def test_old_game_status_is_not_live(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / "status.json").write_text(json.dumps({"heartbeat_ms": 0, "state": "loaded"}))
            self.assertIsNone(reload.read_status(folder))
            (folder / "status.json").write_text("{")
            self.assertIsNone(reload.read_status(folder))

    def test_failed_replacement_is_not_acknowledged(self):
        status = {"request": "new.dll", "state": "rejected", "pid": 12, "error": "wrong ABI"}
        with patch.object(reload, "read_status", return_value=status):
            with self.assertRaisesRegex(RuntimeError, "wrong ABI"):
                reload.wait_for_request(Path("."), "new.dll", "new-build", 1, 12)

    def test_game_restart_is_not_hot_reload(self):
        status = {"request": "new.dll", "state": "loaded", "pid": 13}
        with patch.object(reload, "read_status", return_value=status):
            with self.assertRaisesRegex(RuntimeError, "PID changed"):
                reload.wait_for_request(Path("."), "new.dll", "new-build", 1, 12)

    def test_publication_preserves_old_dll(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            module = folder / "source.dll"
            module.write_bytes(b"new module")
            (folder / "old.dll").write_bytes(b"old module")
            with patch.object(reload, "read_status", return_value=None), patch.object(reload, "wait_for_request") as wait:
                reload.publish(folder, module, "build", 1)
            name = (folder / "request.txt").read_text().strip()
            self.assertEqual((folder / name).read_bytes(), b"new module")
            self.assertEqual((folder / "old.dll").read_bytes(), b"old module")
            self.assertEqual(wait.call_args.args[1], name)

    def test_no_actor_is_not_a_gameplay_pass(self):
        status = {"state": "loaded", "pid": 12, "generation": 1, "actors": {}}
        with patch.object(reload, "read_status", return_value=status), \
             patch.object(reload.time, "monotonic", side_effect=[0, 0, 2]), \
             patch.object(reload.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "No verified displacement"):
                reload.observe(Path("."), 0xFF001234, 1, 2)


if __name__ == "__main__":
    unittest.main()
