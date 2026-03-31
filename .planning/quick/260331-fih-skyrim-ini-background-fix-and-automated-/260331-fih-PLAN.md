---
phase: quick
plan: 260331-fih
type: execute
wave: 1
depends_on: []
files_modified:
  - scripts/smoke_test.sh
autonomous: true
must_haves:
  truths:
    - "Skyrim does not pause/freeze when window loses focus under Wine/Proton"
    - "smoke_test.sh runs headlessly, launches server+replay client, and reports connection success/failure"
  artifacts:
    - path: "scripts/smoke_test.sh"
      provides: "Automated headless connection smoke test"
  key_links:
    - from: "scripts/smoke_test.sh"
      to: "skyrim-coop --host-only / --replay"
      via: "spawns both processes, checks logs"
---

<objective>
Fix Skyrim background-focus freeze and create an automated headless smoke test.

Purpose: (1) Skyrim pauses when the window loses focus under Wine/Proton, blocking multi-instance testing. The well-known `bAlwaysActive=1` INI setting prevents this. (2) Need a repeatable headless script that launches the server + replay client and verifies connection without manual window interaction.

Output: Patched Skyrim.INI with background-active setting; `scripts/smoke_test.sh` for automated smoke testing.
</objective>

<context>
@.planning/STATE.md
@Code/native_client/main.cpp (replay mode entry, --host-only and --replay flags)

INI location (MO2 Proton prefix):
  /home/bighass/.steam/steam/steamapps/compatdata/2158446106/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.INI

Built binaries:
  - skyrim-coop (project root): native_client binary with --host-only and --replay modes
  - build/linux/x86_64/releasedbg/skyrim-coop: same binary in build output

Replay fixtures: tests/fixtures/replay/enet/*.ndjson (auth_flow.ndjson, two_player_host.ndjson, etc.)
</context>

<tasks>

<task type="auto">
  <name>Task 1: Patch Skyrim.INI for background activity</name>
  <files>/home/bighass/.steam/steam/steamapps/compatdata/2158446106/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.INI</files>
  <action>
Add `bAlwaysActive=1` under the existing `[General]` section in Skyrim.INI. This is the standard Bethesda INI setting that prevents the game engine from pausing/throttling when the window loses focus. The setting goes right after the `[General]` header line.

The current `[General]` section contains:
```
[General]
sLanguage=ENGLISH
sTestFile1=Dawnguard.esm
...
```

Insert `bAlwaysActive=1` as the second line under `[General]`, before `sLanguage`.

Do NOT modify SkyrimPrefs.ini (it does not contain this setting). Do NOT create a new section.
  </action>
  <verify>
    <automated>grep -c "bAlwaysActive=1" "/home/bighass/.steam/steam/steamapps/compatdata/2158446106/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/Skyrim.INI"</automated>
  </verify>
  <done>Skyrim.INI has bAlwaysActive=1 under [General]. Game will no longer pause when window loses focus.</done>
</task>

<task type="auto">
  <name>Task 2: Create headless connection smoke test script</name>
  <files>scripts/smoke_test.sh</files>
  <action>
Create `scripts/smoke_test.sh` (chmod +x) that performs a headless automated smoke test:

1. Set PROJECT_ROOT to the script's directory parent (the repo root).
2. Define BINARY as `$PROJECT_ROOT/skyrim-coop` (the native_client binary). Fail early if it does not exist.
3. Define REPLAY_FILE as `$PROJECT_ROOT/tests/fixtures/replay/enet/auth_flow.ndjson`. Fail early if missing.
4. Create a temp dir for logs.
5. Start the server: `$BINARY --host-only --server-port 10578` with stdout/stderr redirected to `$TMPDIR/server.log`. Background it, capture PID.
6. Wait 3 seconds for the server to initialize.
7. Start the replay client: `$BINARY --replay $REPLAY_FILE` with stdout/stderr to `$TMPDIR/client.log`. Background it, capture PID.
8. Wait 10 seconds for the replay to complete (replay mode exits on its own, but cap with timeout).
9. Kill both processes (server and client if still running) via `kill -TERM $PID 2>/dev/null`.
10. Check results:
    - If client.log contains "Replayed" and a count > 0, report SUCCESS.
    - If server.log contains "GameServer initialized" or similar startup message, note server started OK.
    - Otherwise report FAILURE.
11. Print both log files for debugging.
12. Clean up temp dir.
13. Exit 0 on success, 1 on failure.

Use `set -euo pipefail`. Use `trap` to ensure cleanup on exit. Use portable bash constructs.

The script should be fully headless -- no X11/Wayland display needed since both --host-only and --replay modes are pure CLI (no Skyrim window).
  </action>
  <verify>
    <automated>bash scripts/smoke_test.sh</automated>
  </verify>
  <done>smoke_test.sh exits 0 when server starts and replay client processes events. Script runs without any GUI or manual interaction.</done>
</task>

</tasks>

<verification>
- `grep "bAlwaysActive=1" "$INI_PATH"` returns 1 match
- `bash scripts/smoke_test.sh` exits 0 and prints SUCCESS
</verification>

<success_criteria>
1. Skyrim.INI patched with bAlwaysActive=1 under [General]
2. scripts/smoke_test.sh runs headlessly, spawns server + replay client, verifies connection, cleans up, exits 0
</success_criteria>

<output>
After completion, report results inline (quick task, no SUMMARY file needed).
</output>
