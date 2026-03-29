---
status: awaiting_human_verify
trigger: "Skyrim crashes during initial cell loading when relay DLL hooks are active. All 31 hooks install successfully, but the game dies ~12s after launch before reaching gameplay."
created: 2026-03-29T00:00:00Z
updated: 2026-03-29T00:01:00Z
---

## Current Focus

hypothesis: CONFIRMED - Three compounding issues cause TCP backpressure that blocks Skyrim's main thread during cell load
test: Code review confirms all three issues
expecting: Fixing all three eliminates crash
next_action: Awaiting user verification that Skyrim no longer crashes during cell load

## Symptoms

expected: Skyrim loads into main menu / save game with DLL hooks active, sending hook events over TCP to native client
actual: Skyrim crashes ~12 seconds after launch during initial cell load. TCP connection resets (send() failed: Connection reset by peer 104). Only character_ctor events captured before crash.
errors: "send() failed: Connection reset by peer (104)" on native client side when trying to send heartbeat — Skyrim process died
reproduction: ./tests/launch_host.sh --record /tmp/session.ndjson — crash during initial cell load every time
started: Current session. DLL log shows clean init: 31/31 hooks created, all enabled, native process connected. First character_ctor with formId=0x800 fires, then many more with formId=0, then crash.

## Eliminated

## Evidence

- timestamp: 2026-03-29T00:00:30Z
  checked: main.cpp line 674 - unconditional `break;` after first message in receive loop
  found: Native client processes exactly 1 TCP message per 16ms frame (~60 msg/sec max), despite kMaxPacketsPerFrame=64 being set
  implication: During cell load burst of 228+ character_ctor events, TCP receive buffer fills up in ~3.8 seconds

- timestamp: 2026-03-29T00:00:30Z
  checked: TcpClient::ReceiveJsonLine() - uses blocking recv() with no poll/select
  found: recv() blocks indefinitely waiting for data. Native client cannot skip to service updates when no data is pending.
  implication: Combined with 1-msg-per-frame, the native client is alternating between blocking-on-recv and 16ms sleep, creating massive bottleneck

- timestamp: 2026-03-29T00:00:30Z
  checked: TcpServer::SendJson() in DLL - synchronous send() inside CRITICAL_SECTION
  found: When TCP kernel buffer fills (receiver not draining fast enough), send() blocks Skyrim's main thread
  implication: Skyrim's main thread stalls during cell load, causing either watchdog kill or internal state corruption

- timestamp: 2026-03-29T00:00:30Z
  checked: InstallAllHooks(bool /*aEssentialOnly*/) - parameter commented out
  found: The essentialOnly parameter is ignored. All 31 hooks install regardless of the flag. relay_main.cpp passes essentialOnly=true by default but it has no effect.
  implication: All 31 hooks fire during cell load instead of just the 6 essential ones, amplifying the message flood

- timestamp: 2026-03-29T00:00:30Z
  checked: character_ctor hooks (HookCharacterCtor, HookCharacterCtor2) - no dirty cache throttling
  found: These hooks always send JSON + TCP for every call, no rate limiting. During cell load, hundreds fire in rapid succession.
  implication: Major contributor to the message flood that fills TCP buffer

## Resolution

root_cause: Three compounding issues cause TCP backpressure that blocks Skyrim's main thread during cell load: (1) Native client processes only 1 TCP message per frame due to unconditional `break;` at main.cpp:674, limiting throughput to ~60 msg/sec. (2) Native client recv() is blocking, so it can't skip to updates when no data is pending. (3) The essentialOnly hook filter is not implemented (parameter commented out), so all 31 hooks fire during cell load instead of 6, flooding the TCP channel with 228+ messages in seconds. When the TCP kernel send buffer fills, the DLL's synchronous send() blocks Skyrim's main thread, causing the game to crash or be killed by an internal watchdog.
fix: (1) Remove `break;` at main.cpp:674 so all pending messages drain per frame (up to kMaxPacketsPerFrame=64). (2) Make TcpClient::ReceiveJsonLine non-blocking using poll() so main loop continues when no data available. (3) Implement essentialOnly filter in InstallAllHooks to only install the 6 essential hooks by default.
verification: Both SkyrimCoopHooksDLL (mingw) and SkyrimCoopNative (linux) build successfully. Binaries deployed to SKSE plugins dir.
files_changed:
  - Code/native_client/main.cpp
  - Code/native_client/game_bridge/tcp_client.cpp
  - Code/relay_dll/hook_trampolines.cpp
