# Bug reports from play

Players catch the evidence while playing, and the developers turn each report into the fastest test that shows
the bug (`docs/design/testing.md`). No screenshots: the snapshot holds what a picture would show of sync bugs, and a
player can press F12 for one. It is built into the player build (`test_harness=n`), and it works the same way
in a test build.

## What a player does

```
something goes wrong   ->  Enter, /bug what happened         (every game writes its part)
end of the session     ->  Enter, /export                    (one zip, its path in chat and the clipboard)
after a crash/freeze   ->  start the game again              (the zip is made at start, no /export needed)
send the newest SkyrimCoop-*.zip from My Games/Skyrim Special Edition/SkyrimCoop/
```

## /bug

```
a game: /bug <text>  -> BugReportRequest{Text}               (BugReportService::Report)
host's rules         -> Id = "2026-10-29-2214-a3f", By, SessionTick
                     -> NotifyBugReport to every game, the host's own too   (HostAuthority::OnBugReport)
every game           -> IBugReportBridge::Write(part)                        (BugReportBridge)
    reports/<id>/<host|guest>-<player>/
      note.txt       id, who typed it, the text, this game, build, unix_ms, session tick
      log.txt        the plugin log, the last 10 minutes (Diagnostics::LogRing; breadcrumbs dumped into it first)
      messages.txt   the last 5 minutes of messages (Diagnostics::MessageTrail): S sent, R received,
                     I/O the host's rules in and out, with the host's clock
      snapshot.json  Diagnostics::Report(): the player, the session, copies, every loaded actor's full state,
                     menus, the frame (the same readouts as the harness status)
      save.txt       the host only: saves/<save>-<write time>.ess, its newest save and co-save, copied into
                     reports/saves/ once however many reports name it (never made: the save freeze is still open)
not in a session     -> this game's part alone, under its own id
```

The snapshot is taken on the main thread, and the files are written on a worker thread.

## A run that ends badly

```
start:      last_run (this process id) written under SkyrimCoop/run<instance>/
clean exit: ExitProcess(0) writes clean_exit                                 (CrashHandler)
next start, before the log opens (Plugin()):
  last_run from another process, and (no clean_exit after it, or a CrashLoggerSSE crash-*.log after it)
    -> reports/<id>-crash/  its tp_client logs, the crash log if any, note.txt (kind: crash | ended)
    -> /export's zip, made right away
```

"Ended" with no crash log means a freeze the player ended, or the game was killed. Test games skip all of this
(`SKYRIMCOOP_NO_CRASH_REPORTS`, set by `coop_test.py`), because `stop` kills them.

## /export

`Diagnostics::Folders::ExportNow` collects everything changed since the last export (`SkyrimCoop/last_export`):

- `reports/**`, the bug reports and crash bundles
- `logs/tp_client*.log`
- `crash/crash-*.log` from CrashLoggerSSE
- `crash/crash_*.dmp`, our minidump

It writes them, plus `info.txt` (build, player, `plugins.txt`), into `SkyrimCoop-<player>-<id>.zip`
(`Diagnostics::ZipWriter`, deflate via zlib). The path the chat shows is the Linux one under Wine
(`wine_get_unix_file_name`).

## On the developer's side

```
scripts/coop_test.py report friend1.zip friend2.zip [--id ID] [--since 120] [--messages]
  every report: its note, all games' log lines on the host's clock (each game shifted by when it wrote its part),
  message counts per game, the copies differing from their owners (position, health, death, weapon, sneaking,
  knock state, combat, equipment), crash bundles through scripts/re/crash_report.py
scripts/coop_test.py report friend1.zip --id ID --boot
  the host's save into both test games as BugReport<id>, then duo --save from it (claim the duo first)
```

## Code

- `Code/client/Diagnostics/`: the shared production library, which the test harness calls as well.
  - `EngineReads`: the engine reads both use.
  - `Snapshot`: the status readouts.
  - `LogRing`, `MessageTrail`.
  - `ReportPaths`, `Zip`, `Export`, `CrashReports`: no engine; tested in `ClientTests`.
  - `Folders`.
- `Services/BugReportService` (every game) and `Bridges/BugReportBridge`. GameTests uses `FakeBugReports`.
- `HostAuthority::OnBugReport`.
