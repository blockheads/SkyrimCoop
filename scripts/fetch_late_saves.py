#!/usr/bin/env python3
"""Fetches the late-game saves quest walks boot from into both test games' save folders.

    scripts/fetch_late_saves.py

The saves: userb1's "Clean Save Collection - Main Quest Complete" (Nexus 45425), 19 vanilla saves along
the main quest. Downloading through the Nexus API needs a premium account's API key in
~/.config/skyrimcoop/nexus_api_key. Each save lands as Late<Name>.ess (`duo --save LateAfterDiplomaticImmunity`).
"""
import json
from pathlib import Path
import re
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
KEY = Path.home() / ".config/skyrimcoop/nexus_api_key"
MOD, FILE = 45425, 184573
OUT = ROOT / "build/coop-test/late-saves"
PREFIXES = ("prefix-host", "prefix-client")


def api(path):
    request = urllib.request.Request(f"https://api.nexusmods.com/v1/{path}", headers={
        "apikey": KEY.read_text().strip(), "Application-Name": "SkyrimCoop-test", "Application-Version": "0.1"})
    with urllib.request.urlopen(request) as response:
        return json.load(response)


def main():
    archive = OUT / "collection.7z"
    if not archive.exists():
        OUT.mkdir(parents=True, exist_ok=True)
        link = api(f"games/skyrimspecialedition/mods/{MOD}/files/{FILE}/download_link.json")[0]["URI"]
        # The link is signed: encode only its spaces.
        request = urllib.request.Request(link.replace(" ", "%20"), headers={"User-Agent": "SkyrimCoop-test"})
        with urllib.request.urlopen(request) as response:
            archive.write_bytes(response.read())
    subprocess.run(["7z", "x", "-y", f"-o{OUT / 'extracted'}", str(archive)], check=True, stdout=subprocess.DEVNULL)
    saves = sorted((OUT / "extracted").rglob("*.ess"))
    for prefix in PREFIXES:
        folder = ROOT / "build/coop-test" / prefix / "pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Saves"
        folder.mkdir(parents=True, exist_ok=True)
        for save in saves:
            (folder / f"Late{re.sub('[^A-Za-z0-9]', '', save.stem)}.ess").write_bytes(save.read_bytes())
    print(f"{len(saves)} saves in both test games: " + ", ".join(f"Late{re.sub('[^A-Za-z0-9]', '', s.stem)}" for s in saves))


if __name__ == "__main__":
    main()
