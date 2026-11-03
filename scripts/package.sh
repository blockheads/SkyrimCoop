#!/usr/bin/env bash
# The player package, for friends (not distributed): extract into the matching Steam Skyrim folder (docs/install.md).
# It includes the plugin without the test harness, UI, bundled start, launcher under Steam's launcher name, and
# runtime-specific pinned vendor files: matching SKSE (skse.silverlock.org), Address Library (Nexus 32444), and Alternate
# Start 4.2.6a (Nexus 272) with the USSEP 4.3.9c it requires (Nexus 266), archives in build/mods.
# Zipped as build/package/SkyrimCoop-<git describe>-skyrim-<runtime>.zip. Its DLL and PDB stay beside it in
# <zip stem>-symbols/ (BUILD: the commit a crash report names; RUNTIME: the matching Skyrim), for
# scripts/re/symbolize_crash.py --build: players' crash logs carry module offsets, never symbols.
# Its own xmake configuration (build/package-config, build/package-build): the test build's is untouched. Dev mode,
# the optimization every test runs, with the PDB for crash logs.
#   scripts/package.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$ROOT/build/toolchains"
[[ -d "$T/msvc-bin" ]] || "$ROOT/scripts/toolchain/setup_msvc_toolchain.sh"
export PATH="$T/msvc-bin:$PATH"
cd "$ROOT"
VERSION="$(python3 -c 'import sys; sys.path.insert(0, "scripts"); from skyrim_runtime import runtime_version; print(runtime_version().removesuffix(".0"))')"
export XMAKE_CONFIGDIR="$ROOT/build/package-config-$VERSION"
VENDOR="${COOP_PACKAGE_VENDOR:-$ROOT/build/runtime-vendor/$VERSION}"
PINS="$ROOT/scripts/package-vendor.sha256"
if [[ "$VERSION" == "1.6.1170" ]]; then
    VENDOR="${COOP_PACKAGE_VENDOR:-$ROOT/build/package-vendor}"
    PINS="$ROOT/scripts/package-vendor-1.6.1170.sha256"
fi
(cd "$VENDOR" && sha256sum --quiet -c "$PINS") ||
    { echo "$VENDOR does not hold the pinned vendor files for Skyrim $VERSION" >&2; exit 1; }
xmake f -P "$ROOT" -p windows -a x64 --toolchain=clang-cl --sdk="$T/winsysroot" -m dev --test_harness=n \
    --skyrim_version="$VERSION" -o "build/package-build/$VERSION" -y >/dev/null
for target in SkyrimTogetherClientDLL SkyrimCoopLauncher; do
    xmake build -P "$ROOT" -j8 "$target"
done
python3 - "$ROOT" "$(git describe --tags --always --dirty)" "$(git rev-parse --short HEAD)" "$VERSION" "$VENDOR" "$PINS" <<'EOF'
import shutil, sys, zipfile
from pathlib import Path

root, version, commit = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
runtime, vendor, pins = sys.argv[4], Path(sys.argv[5]), Path(sys.argv[6])
built = root / f"build/package-build/{runtime}/windows/x64/dev"
dll = built / "SkyrimTogetherClient.dll"
if b"SkyrimCoopHotReload" in dll.read_bytes():
    sys.exit(f"{dll} has the test harness: the package needs --test_harness=n")
ui = root / "Code/client/Ui/assets"
files = {
    "Data/SKSE/Plugins/SkyrimTogetherClient.dll": dll,
    "Data/SkyrimCoopStart.esp": root / "GameFiles/Skyrim/SkyrimCoopStart.esp",
    "SkyrimSELauncher.exe": built / "SkyrimCoopLauncher.exe",
    "INSTALL.md": root / "docs/install.md",
    "playtest.md": root / "docs/playtest.md",
    "LICENSE": root / "LICENSE",
}
for line in pins.read_text().splitlines():
    name = line.split("  ", 1)[1]
    files[name] = vendor / name
# The UI's fixtures are its tests' inputs (scripts/check-ui.sh), not the game's.
for source in ui.rglob("*"):
    if source.is_file() and "fixtures" not in source.relative_to(ui).parts:
        files[f"Data/SKSE/Plugins/SkyrimCoop/ui/{source.relative_to(ui).as_posix()}"] = source
out = root / f"build/package/SkyrimCoop-{version}-skyrim-{runtime}.zip"
out.parent.mkdir(parents=True, exist_ok=True)
# Sorted entries with fixed dates and modes: the same files make the same zip.
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
    for name in sorted(files):
        entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        entry.external_attr = 0o644 << 16
        entry.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(entry, files[name].read_bytes())
symbols = out.parent / f"{out.stem}-symbols"
symbols.mkdir(exist_ok=True)
for name in ("SkyrimTogetherClient.dll", "SkyrimTogetherClient.pdb"):
    shutil.copy2(built / name, symbols / name)
(symbols / "BUILD").write_text(commit + "\n")
(symbols / "RUNTIME").write_text(runtime + "\n")
print(f"{out} ({out.stat().st_size // 1024} KB, {len(files)} files); symbols in {symbols.name}")
EOF
