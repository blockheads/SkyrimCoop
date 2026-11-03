# Finding network and join costs

Built 2026-11-03 for Steam Skyrim 1.7.104. The normal optional runtime configuration is unchanged;
this batch does not build or validate the legacy runtime.

## Readings for players and developers

Type `/perf` in chat for frame/update p95, working memory, network message rates, the largest message types,
named connections' RTT/loss, and the latest world join. `/bug what happened` records each game's own snapshot;
`/export` bundles those reports. There is no periodic performance-report exchange between players.

The snapshot's `game.join.timings` separates cache preparation, connecting/checking, character creation,
waiting for the host's save, receiving data, decompression/verification, writing, Skyrim loading and applying the
player's character. Total also includes placement and the final reconnect. Transfer starts on the first chunk;
waiting includes save creation and time until that first chunk. Host logs include native save duration.
`load_ms` starts after writing and ends at the engine's load event. These are monotonic elapsed times.

`perf.messages.top` sorts serialized application traffic by message name, direction and full connection id.
`local_top` and `local_bytes` describe in-process routes separately. Host I/O and a guest's send/receive are each
counted once in that game's network totals. These counts precede transport compression. The requested window is
at most one minute; the bounded message trail can retain less under an unusually heavy load.

`line.connections` contains named actual ENet connections, with RTT, RTT variation, the current reliable loss
estimate, reliable bytes awaiting acknowledgement and queued reliable send commands. Loss is ENet's smoothed
estimate, not a cumulative percentage of game messages. `line.wire_totals` contains ENet datagram bytes and
packet counts (compression, protocol and retransmissions included; Steam tunnel framing excluded). Its native
32-bit counters wrap and reset with the transport; difference samples modulo 2^32 within one transport lifetime.
The older `line.sent_bytes`/`recv_bytes` remain the guest's last-second payload counters; they are zero on the
host's in-process route. Use the message rows and wire totals for the host's actual network.

Detailed CPU zones, memory and hook rates already use [Tracy](https://github.com/wolfpld/tracy):
`scripts/coop_test.py profile --seconds 20 [--stacks]`. New zones cover host save preparation and received world
blocks. Player builds include the cheap readings and report snapshots; Tracy remains a developer build option.
The existing generic counters work in another agent's branch is independent of this change.

## Reusing a received world

Both games already have identical map assets. What travels is the host's current `.ess` save and SKSE `.skse`
co-save. Skyrim must still load that current world; reusing an older world would lose quests and world changes.

The joining game reads its previous `SkyrimCoopJoin` save and co-save, which the old join flow already left on
disk. No extra growing cache directory or host-specific path is introduced. The packed pair is split at
content-dependent boundaries (8 to 24 KiB, average about 16 KiB). SHA-256 identifies each raw block; each missing
block is compressed independently with Snappy. A small hash list travels with the initial authentication;
only after the normal build/runtime/mod/character gate passes does the host use it. Matching blocks travel as
references; changed or absent blocks travel in full. The guest reconstructs and verifies every block, regardless
of whether it came from the network or disk, before writing/loading the host's exact current save. Different
hosts cannot reuse differing bytes merely because their save names match. Corrupt cached bytes have different
fingerprints and are automatically sent again. Missing references, bad hashes, invalid compression, gaps and
mixed totals fail before loading. Working cache memory is released on completion, failure or cancel.

The packed pair is bounded to 128 MiB, block hashes to 16,384, compressed blocks to 32 KiB, and decompression to
the declared 24 KiB maximum. Bulk transfers budget 256 KiB/update and at most 64 blocks/update, and wait when ENet
already has more than 512 reliable send commands queued. Several waiting games share one host snapshot. Party
reloads send a full fresh snapshot without assuming a cache negotiated on an earlier connection.

Research: [rsync](https://rsync.samba.org/) and [restic's chunker](https://github.com/restic/chunker) demonstrate
reusing verified content rather than old file names. The small deterministic Gear splitter follows the
[content-defined chunking approach](https://www.usenix.org/conference/atc16/technical-sessions/presentation/xia),
without introducing a full backup/sync runtime. Windows uses [CNG SHA-256](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcrypthash), with the
unmodified MIT [PicoSHA2](https://github.com/okdshin/PicoSHA2) header as the portable fallback, pinned in
`Code/external/picosha2/SOURCE.md`; its license is included in player packages. Compression remains [Snappy](https://github.com/google/snappy), and connection
readings come from our existing [ENet6](https://github.com/SirLynix/enet6) transport.

## Evidence and limits

Baseline: two native kept-character joins passed on 1.7.104. Host sample one created 2,774,916 game bytes plus
3,830 SKSE bytes and queued 76 old chunks in about 0.14 seconds on loopback. Guest loading/arrival took seconds.
The two real saves are different. Compressing the entire packed save yielded zero matching 32 KiB chunks.
Using independent content-defined blocks reused 540,167 raw bytes (32/158 blocks):

| Payload | Bytes |
| --- | ---: |
| Old whole-save Snappy | 2,487,486 |
| New cold blocks | 2,513,992 |
| New warm blocks | 2,187,608 |

This pair saves about 12% versus the old transfer, plus roughly 5 KiB of advertised hashes and block headers.
Cold payload is about 1% larger. The production-code offline benchmark ran cache creation, splitting,
verification and reconstruction in about 44 ms optimized on this machine. Identical snapshots can reuse every
payload block; large native compressed changes can substantially reduce reuse. These are measurements of this
pair, not a promised loading-time improvement on Windows or every save.

Repeat with actual saves (read-only):

```
scripts/xmake-linux.sh build -j8 SaveTransferBench
build/linux/x86_64/debug/SaveTransferBench OLD.ess OLD.skse NEW.ess NEW.skse
```

The pure tests cover exact reconstruction, inserted data, stale/corrupt cache, missing references, bad SHA,
compressed expansion, bounds, order, repetition and mixed transfers. The real-service GameTests repeatedly join
from a retained save, then change the host's lock state and corrupt cached files; the guest must get the current
world. Native acceptance: `guest-reuses-world-cache` starts two fresh guest processes and checks cold/warm
transfers, named connection readings and `/perf` through normal game input.

## Final native checks

Steam 1.7.104, 2026-11-03: `guest-reuses-world-cache` passed twice, including two fresh guest processes each time,
`/perf` through normal chat input in each game, and named connection diagnostics. Cold/warm payload pairs were
2,505,509 / 705,863 bytes and 2,505,417 / 874,112 bytes (72% and 65% reduction respectively). The second pair used
CNG: cache preparation 17 ms and decode/verification 13 ms, compared with about 185 / 181 ms with the portable
hash in the development DLL. Native Skyrim loading remained about 1.2 seconds in these small QASmoke samples.
A cold transfer varied from 0.49 to 5.68 seconds across runs; the timings expose this variability rather than
assuming every delay is disk loading. No Windows large-save loading-time claim is made from these samples.

`host-quickloads-party` passed on both the saved prior-build control and the final build. One earlier run flagged
a small ladle rotation difference after the reload; that comparison did not recur in the final repeats.
`bug-report-writes-every-game` passed, including export. Fast checks: 209 ClientTests, 199 HostAuthorityTests,
33 encoding cases, 291 GameTests flows, and 32 Python harness checks.

Launcher: normal menu Play and `--graphics +connect_lobby 0` both booted SKSE and our plugin; the invitation
argument skipped the menu. Bethesda's graphics window and Options opened, and selecting a preset wrote the
expected settings in the same prefix. Full graphical save-and-close automation did not complete reliably in the
headless test environment, so the Windows desktop save/return flow and a live two-account Steam cold invitation
remain manual acceptance checks. The original launcher's own Play button still starts vanilla Skyrim; use our
Play co-op button after closing settings.

The native launcher investigation also found a test-fixture fault: Bethesda opens `Debug.log` in its working
directory and unconditionally prints to that stream. The test game's read-only link to the player's Steam log
made that fail with CRT exception `c0000417`. `ensure_duo_game` and package checks now keep mutable logs private;
the Python regression verifies that writing the test log never changes the source Steam log. No filename
workaround or extra copy of Bethesda's executable is needed. Startup/exit failures are recorded in the bounded
`Documents/My Games/Skyrim Special Edition/SkyrimCoop/launcher.log`.
