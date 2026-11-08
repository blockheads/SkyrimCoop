"""Portable runtime selection and reference-library decoding checks."""
import os
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

import skyrim_runtime as runtime
sys.path.insert(0, str(Path(__file__).parent / "re"))
from address_library import load


class RuntimeTests(unittest.TestCase):
    def test_runtime_switch_deploys_older_same_size_plugin_and_loader(self):
        from coop_harness.runtime import Runtime
        from coop_harness import runtime as harness_runtime
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, plugins = root / "legacy-build", root / "Plugins"
            source.mkdir()
            dll, loader = source / "SkyrimTogetherClient.dll", source / "SkyrimCoopLoader.dll"
            for path in (dll, loader):
                path.write_bytes(b"old")
                os.utime(path, (1000, 1000))
            for path in (plugins / loader.name, plugins / "SkyrimCoop" / dll.name):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"new")
                os.utime(path, (2000, 2000))
            ctx = SimpleNamespace(require_test_harness=lambda: None, deploy_ui=lambda: None,
                                  copy_if_changed=lambda *args, **kwargs: Runtime.copy_if_changed(None, *args, **kwargs))
            with patch.object(harness_runtime, "DLL", dll), patch.object(harness_runtime, "LOADER", loader), \
                 patch.object(harness_runtime, "PLUGINS", plugins), patch.object(harness_runtime, "log"):
                Runtime.deploy_dll(ctx)
            self.assertEqual((plugins / loader.name).read_bytes(), b"old")
            self.assertEqual((plugins / "SkyrimCoop" / dll.name).read_bytes(), b"old")

    def test_reviewed_lock_patterns_follow_runtime_and_never_hide_live_overlaps(self):
        from coop_harness.observations import Observations
        from coop_harness import observations, common
        root = Path(__file__).resolve().parent
        old = "SkyrimSE.exe+0xe85437 > SkyrimSE.exe+0x17928d vs SkyrimSE.exe+0x16abbd > SkyrimSE.exe+0xe92c16"
        new = "SkyrimSE.exe+0x104ad57 > SkyrimSE.exe+0x17e87d vs SkyrimSE.exe+0x17014d > SkyrimSE.exe+0x10587a6"
        for version, filename, reviewed, other in (
            ("1.6.1170.0", "lock_order_known.txt", old, new),
            ("1.7.104.0", "lock_order_known-1.7.104.txt", new, old),
        ):
            if common.SKYRIM_VERSION == version:
                self.assertEqual(common.KNOWN_LOCK_PATTERNS.name, filename)
            ctx = Mock()
            ctx.known_lock_patterns.side_effect = lambda: Observations.known_lock_patterns(ctx)
            reports = [f"[ours] lock order inversion [{reviewed}]",
                       f"[ours] live lock order inversion [{reviewed}]",
                       f"[ours] lock order inversion [{other}]"]
            ctx.game.return_value.read_status.return_value = {"lock_order": {"reports": reports}}
            with patch.object(observations, "KNOWN_LOCK_PATTERNS", root / filename):
                found = Observations.lock_order_reports(ctx)
                self.assertNotIn(reviewed, found)
                self.assertIn("live: " + reviewed, found)
                self.assertIn(other, found)

    def test_lock_orders_the_frame_keeps_apart_never_fail_and_never_hide_a_risk(self):
        from coop_harness.observations import Observations
        sites = "SkyrimSE.exe+0x1 > SkyrimSE.exe+0x2 vs SkyrimSE.exe+0x3 > SkyrimSE.exe+0x4"
        ctx = Mock()
        ctx.known_lock_patterns.return_value = set()
        reports = [f"[ours] kept apart by the frame: lock order inversion [{sites}]: thread 1 holds",
                   f"[ours] lock order inversion [{sites}]: thread 2 holds",
                   f"[ours] live lock order inversion [{sites}]: thread 1 holds"]
        ctx.game.return_value.read_status.return_value = {"lock_order": {"reports": reports}}
        found = Observations.lock_order_reports(ctx)
        self.assertFalse(found["kept apart: " + sites][2])
        self.assertTrue(found[sites][2])
        self.assertIn("live: " + sites, found)
        # Once the two stretches ran at once in that game, a kept-apart report fails like any of ours.
        ctx.game.return_value.read_status.return_value = {"lock_order": {"reports": reports[:1], "stretches_overlapped": True}}
        self.assertTrue(Observations.lock_order_reports(ctx)[sites][2])

    def test_switching_runtime_replaces_an_older_same_size_loader(self):
        from coop_harness.runtime import Runtime
        with tempfile.TemporaryDirectory() as folder:
            source, target = Path(folder) / "legacy-loader", Path(folder) / "staged-loader"
            source.write_bytes(b"old")
            target.write_bytes(b"new")
            os.utime(source, (1000, 1000))
            os.utime(target, (2000, 2000))
            with patch("coop_harness.runtime.log"):
                self.assertTrue(Runtime.copy_if_changed(None, source, target, compare_contents=True))
                self.assertEqual(target.read_bytes(), b"old")
                self.assertFalse(Runtime.copy_if_changed(None, source, target, compare_contents=True))

    def test_latest_and_legacy_files_are_distinct(self):
        with patch.dict(os.environ, {"COOP_SKYRIM_VERSION": "latest"}):
            self.assertEqual(runtime.runtime_version(), "1.7.104.0")
            self.assertEqual(runtime.skse_dll(), "skse64_1_7_104.dll")
        self.assertEqual(runtime.library_name("1.6.1170"), "versionlib-1-6-1170-0.bin")
        with self.assertRaises(ValueError):
            runtime.runtime_version("1.7.99")
        self.assertEqual(runtime.runtime_from_log("SkyrimSE v1.6.1170"), "1.6.1170.0")
        self.assertEqual(runtime.runtime_from_log("Skyrim 1.7.104 with SKSE 2.3.1"), "1.7.104.0")

    def test_staging_selects_matching_sibling_without_replacing_steam_exe(self):
        def executable(path, parts):
            path.write_bytes(struct.pack("<4I", 0xFEEF04BD, 0x10000, parts[0] << 16 | parts[1], parts[2] << 16 | parts[3]))
        with tempfile.TemporaryDirectory() as folder, patch.dict(os.environ, {}, clear=True):
            root = Path(folder)
            old, new = root / "SkyrimSE.exe", root / "SkyrimSE_1.7.104.exe"
            executable(old, (1, 6, 1170, 0))
            executable(new, (1, 7, 104, 0))
            self.assertEqual(runtime.game_executable(root), new)
            self.assertEqual(runtime.game_executable(root, "1.6.1170"), old)
            self.assertEqual(runtime.pe_version(old), "1.6.1170.0")
            with patch.dict(os.environ, {"SKYRIM_EXECUTABLE": str(old)}):
                with self.assertRaises(ValueError): runtime.game_executable(root)

    def test_v5_decoder_skips_holes_and_rejects_wrong_version_or_truncation(self):
        data = struct.pack("<5I64s3I4I", 5, 1, 7, 104, 0, b"SkyrimSE.exe", 8, 0, 4, 0, 0x1000, 0, 0x2000)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "library.bin"
            path.write_bytes(data)
            self.assertEqual(load(path, "1.7.104.0"), {1: 0x1000, 3: 0x2000})
            with self.assertRaises(ValueError): load(path, "1.6.1170.0")
            for end in range(len(data)):
                path.write_bytes(data[:end])
                with self.assertRaises(ValueError): load(path)

    def test_reference_scan_uses_only_the_selected_runtime_hook(self):
        import build_re_data
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            client = root / "Code/client"
            client.mkdir(parents=True)
            (client / "hooks.cpp").write_text('#if COOP_SKYRIM_1_6_1170\nHOOK_ID(52488, &RealOkay, HookOkay);\n#else\nHOOK_ID(524025, &RealOkay, HookOkay);\n#endif\n')
            with patch.object(build_re_data, "ROOT", root), patch.object(build_re_data, "CLIENT", client):
                for version, expected in (("1.6.1170.0", 52488), ("1.7.104.0", 524025)):
                    with patch.object(build_re_data, "EXE_VERSION", version):
                        self.assertEqual(set(build_re_data.scan_our_code()), {expected})

    def test_ida_import_rejects_a_different_binary_before_any_database_write(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            exe = root / "SkyrimSE.exe"
            exe.write_bytes(struct.pack("<4I", 0xFEEF04BD, 0x10000, 1 << 16 | 7, 104 << 16))
            annotations = root / "annotations.json"
            modules = {name: Mock() for name in ("ida_auto", "ida_bytes", "ida_funcs", "ida_kernwin", "ida_nalt", "ida_name", "ida_netnode", "idc")}
            modules["ida_nalt"].get_input_file_path.return_value = str(exe)
            modules["ida_nalt"].retrieve_input_file_sha256.return_value = bytes.fromhex("ab" * 32)
            path = Path(__file__).parent / "re/ida_apply.py"
            for version, digest in (("1.6.1170.0", "ab" * 32), ("1.7.104.0", "cd" * 32)):
                annotations.write_text(json.dumps({"exe_version": version, "input_sha256": digest}))
                with patch.dict(sys.modules, modules), patch.dict(os.environ, {"COOP_RE_ANNOTATIONS": str(annotations)}):
                    spec = importlib.util.spec_from_file_location("runtime_ida_apply_test", path)
                    module = importlib.util.module_from_spec(spec)
                    spec.loader.exec_module(module)
                    with self.assertRaises(RuntimeError): module.main()
                    modules["ida_netnode"].netnode.assert_not_called()


if __name__ == "__main__":
    unittest.main()
