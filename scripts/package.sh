#!/usr/bin/env bash
# The player package: the plugin without the test harness, its UI, the bundled start, the Steam launcher and the
# install notes, zipped as build/package/SkyrimCoop-<git describe>.zip (docs/install.md says what goes where).
# Its own xmake configuration (build/package-config, build/package-build): the test build's is untouched. Dev mode,
# the optimization every test runs, with the PDB for crash logs.
#   scripts/package.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$ROOT/build/toolchains"
[[ -d "$T/msvc-bin" ]] || "$ROOT/scripts/toolchain/setup_msvc_toolchain.sh"
export PATH="$T/msvc-bin:$PATH"
export XMAKE_CONFIGDIR="$ROOT/build/package-config"
cd "$ROOT"
xmake f -P "$ROOT" -p windows -a x64 --toolchain=clang-cl --sdk="$T/winsysroot" -m dev --test_harness=n \
    -o build/package-build -y >/dev/null
for target in SkyrimTogetherClientDLL SkyrimCoopLauncher; do
    xmake build -P "$ROOT" -j8 "$target"
done
python3 - "$ROOT" "$(git describe --tags --always --dirty)" <<'EOF'
import sys, zipfile
from pathlib import Path

root, version = Path(sys.argv[1]), sys.argv[2]
built = root / "build/package-build/windows/x64/dev"
dll = built / "SkyrimTogetherClient.dll"
if b"SkyrimCoopHotReload" in dll.read_bytes():
    sys.exit(f"{dll} has the test harness: the package needs --test_harness=n")
ui = root / "Code/client/Ui/assets"
files = {
    "Data/SKSE/Plugins/SkyrimTogetherClient.dll": dll,
    "Data/SKSE/Plugins/SkyrimTogetherClient.pdb": built / "SkyrimTogetherClient.pdb",
    "Data/SkyrimCoopStart.esp": root / "GameFiles/Skyrim/SkyrimCoopStart.esp",
    "SkyrimCoopLauncher.exe": built / "SkyrimCoopLauncher.exe",
    "INSTALL.md": root / "docs/install.md",
    "playtest.md": root / "docs/playtest.md",
    "LICENSE": root / "LICENSE",
}
# The UI's fixtures are its tests' inputs (scripts/check-ui.sh), not the game's.
for source in ui.rglob("*"):
    if source.is_file() and "fixtures" not in source.relative_to(ui).parts:
        files[f"Data/SKSE/Plugins/SkyrimCoop/ui/{source.relative_to(ui).as_posix()}"] = source
out = root / f"build/package/SkyrimCoop-{version}.zip"
out.parent.mkdir(parents=True, exist_ok=True)
# Sorted entries with fixed dates and modes: the same files make the same zip.
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(files):
        entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        entry.external_attr = 0o644 << 16
        entry.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(entry, files[name].read_bytes())
print(f"{out} ({out.stat().st_size // 1024} KB, {len(files)} files)")
EOF
