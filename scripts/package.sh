#!/usr/bin/env bash
# One MO2-installable archive of the mod in build/package, its symbols beside it (docs/design/launcher.md, Releases).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
T="$ROOT/build/toolchains"
[[ -d "$T/msvc-bin" ]] || "$ROOT/scripts/toolchain/setup_msvc_toolchain.sh"
export PATH="$T/msvc-bin:$PATH"
cd "$ROOT"
VERSION="$(python3 -c 'import sys; sys.path.insert(0, "scripts"); from skyrim_runtime import runtime_version; print(runtime_version().removesuffix(".0"))')"
export XMAKE_CONFIGDIR="$ROOT/build/package-config-$VERSION"
xmake f -P "$ROOT" -p windows -a x64 --toolchain=clang-cl --sdk="$T/winsysroot" -m dev --test_harness=n \
    --skyrim_version="$VERSION" -o "build/package-build/$VERSION" -y >/dev/null
xmake build -P "$ROOT" -j8 SkyrimTogetherClientDLL
python3 - "$ROOT" "$(git describe --tags --always --dirty)" "$(git rev-parse --short HEAD)" "$VERSION" <<'EOF'
import shutil, sys, zipfile
from pathlib import Path

root, version, commit = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
runtime = sys.argv[4]
built = root / f"build/package-build/{runtime}/windows/x64/dev"
dll = built / "SkyrimTogetherClient.dll"
if b"SkyrimCoopHotReload" in dll.read_bytes():
    sys.exit(f"{dll} has the test harness: the package needs --test_harness=n")
if str(Path.home()).encode() in dll.read_bytes():
    sys.exit(f"{dll} names this machine's home folder: not for a public release")
ui = root / "Code/client/Ui/assets"
# Paths relative to Data/ (MO2's mod root); the documents live in the plugin's own folder, not loose in Data.
docs = "SKSE/Plugins/SkyrimCoop"
files = {
    "SKSE/Plugins/SkyrimTogetherClient.dll": dll,
    "SkyrimCoopStart.esp": root / "GameFiles/Skyrim/SkyrimCoopStart.esp",
    f"{docs}/INSTALL.md": root / "docs/install.md",
    f"{docs}/playtest.md": root / "docs/playtest.md",
    f"{docs}/LICENSE": root / "LICENSE",
    f"{docs}/NOTICE": root / "NOTICE",
    f"{docs}/licenses/PicoSHA2.txt": root / "Code/external/picosha2/LICENSE",
    f"{docs}/licenses/zstd.txt": root / "Code/external/licenses/zstd.txt",
    f"{docs}/licenses/lz4.txt": root / "Code/external/licenses/lz4.txt",
}
# The UI's fixtures are its tests' inputs (scripts/check-ui.sh), not the game's.
for source in ui.rglob("*"):
    if source.is_file() and "fixtures" not in source.relative_to(ui).parts:
        files[f"SKSE/Plugins/SkyrimCoop/ui/{source.relative_to(ui).as_posix()}"] = source


def write(out, contents):
    # Sorted entries with fixed dates and modes: the same files make the same zip.
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(contents):
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.external_attr = 0o644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, contents[name].read_bytes())
    print(f"{out} ({out.stat().st_size // 1024} KB, {len(contents)} files)")


out = root / f"build/package/SkyrimCoop-{version}-skyrim-{runtime}.zip"
out.parent.mkdir(parents=True, exist_ok=True)
write(out, files)
symbols = out.parent / f"{out.stem}-symbols"
symbols.mkdir(exist_ok=True)
for name in ("SkyrimTogetherClient.dll", "SkyrimTogetherClient.pdb"):
    shutil.copy2(built / name, symbols / name)
(symbols / "BUILD").write_text(commit + "\n")
(symbols / "RUNTIME").write_text(runtime + "\n")
print(f"symbols in {symbols}")
EOF
