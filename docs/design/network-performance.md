# Finding network and join costs

Built 2026-11-03 for Steam Skyrim 1.7.104. The normal optional runtime configuration is unchanged;
this batch does not build or validate the legacy runtime.

## Readings for players and developers

Type `/perf` in chat for frame/update p95, working memory, network message rates, the largest message types,
named connections' RTT/loss, and the latest world join. `/bug what happened` records each game's own snapshot;
`/export` bundles those reports. There is no periodic performance-report exchange between players.

The snapshot's `game.join.timings` separates connecting/checking, character creation,
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

## The save's transfer

Replaced 2026-11-16 by save sync ([join-flow.md](join-flow.md#save-sync-2026-11-16)): the save travels as its
difference from one the guest holds, and each save the host makes goes ahead of any load of it. The block reuse built
here on 2026-11-03 (content-defined 16 KiB blocks of the files, SHA-256, Snappy) is gone: on a real session's saves the
engine's LZ4 left 3-23% of blocks shared. `game.join.timings` now gives `world_bytes` (the image), `payload_bytes`
(what came), `pieces`, `held_saves`, `against_held` and `from_store`.

Repeat with actual saves (read-only):

```
scripts/xmake-linux.sh build -j8 SaveTransferBench
build/linux/x86_64/debug/SaveTransferBench OLD.ess OLD.skse NEW.ess NEW.skse
```

The bounds: an image of at most 256 MiB, pieces of 32 KiB, a decode to exactly the declared size, the result checked
against its SHA-256 before it is stored or loaded. A join's or a load's save budgets 256 KiB an update and waits while
ENet has more than 512 reliable commands queued; a save sent ahead one piece an update while fewer than 32 wait.

## Steam's send rate (2026-11-23)

Over Steam a save travelled at 120-240 KB/s (playtests of 2026-10-31 and 2026-11-22: a 1 MB save sent ahead took
8-10 s, so a load soon after a save often sent it again). `SteamBridge` sets Steam's send rate to 4 MB/s (min = max)
and its send buffer to 2 MB, on the listen socket and on `ConnectP2P`.

How the two layers pace (read in their source):

- **Steam** (GameNetworkingSockets `SNP_ThinkSendState`, `SNP_ClampSendRate`): a token bucket at
  `SendRateMin`, queueing what is over it; no bandwidth estimation (min == max is "application has disabled
  bandwidth estimation"; otherwise the first estimate is only clamped, so min wins; Valve: issues #108, #198, #270).
  Skyrim's SDK header gives the default as 256K.
- **ENet** (enet6 6.1.3, `protocol.c`, `peer.c`): at most `packetThrottle/32 x 64 KiB` of reliable data
  unacknowledged; a command is resent after its round trip plus 4 variances (doubling each time), and
  `enet_peer_throttle` cuts the throttle when a round trip comes in above the last plus 2 variances.

So Steam's rate must drain ENet's window faster than ENet's resend timer: ENet sends its 64 KiB once a frame, and at
1 MB/s Steam takes 62 ms to put it out, the tail waiting about 135 ms against a timer of about 100 ms. ENet resends
packets that were only queued, sees a longer round trip and cuts its throttle, sometimes to 0/32 for seconds: the
stalled start. Counted at each hop of the tunnel during a 5.2 s transfer: the host's ENet sent 1,141 datagrams, the
guest's received all of them, yet ENet counted 154 lost.

Measured on the two-account duo (`guest-joins-by-invite`, the guest's store emptied so the whole 1.38 MB save
travels; the rate Steam applies read back through `GetConnectionRealTimeStatus`):

- default (256 KB/s): transfer 7.3-10.2 s (six joins), joined in 9-12 s
- min 256 KB/s, max 4 MB/s: Steam applied 262144 B/s, transfer 7.1-8.8 s: it does not adapt between them
- 1 MB/s: 1.6-2.5 s, but 4 of 12 transfers stalled (3.5-5.4 s); relays only 2.0, 2.2, 5.2 s
- 64 MB/s, relays only (`P2P_Transport_ICE_Enable` off): 2.0, 2.3, 13.0 s; ENet resent 98-301 packets a transfer
- 4 MB/s: 1.35-1.59 s over six joins (three relayed), no stall, ENet's throttle at 16/32 at its lowest
- 1 MB/s with ENet's throttle never cut (deceleration 0): 1.66-1.89 s, no stall; not kept, since the throttle is the
  only back-off on a slow upload

What caps a save now is ENet: 64 KiB a round trip, and its round trip through the tunnel is about 80 ms where Steam's
ping is 17 ms (each end pumps the tunnel once a frame, `docs/ideas.md`). Valve's relays police a rate (#226): three
relayed joins at 4 MB/s were as fast as direct ones. Not measured on a real home line: on an upload slower than
32 Mbit/s Steam sends faster than the line carries, and ENet's throttle has to back off.

## Native checks of 2026-11-03 (the block reuse, since replaced)

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
