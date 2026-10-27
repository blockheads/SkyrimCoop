# Host loads take the party along

Feature 16. A host quickload or chosen save load rolls back every game to that save, using the party wipe's
reload. Each guest rejoins as the character the save keeps, at that character's saved place. A guest cannot load
its own save while connected or joining; every attempt explains why in chat:

> Only the host loads saves: your game can't load while you're in a co-op session.

## Flow

`BGSSaveLoadManager::Load_Impl` (35728) handles menu and console loads. Quickload queues request `0xD0000400`
through 35769; its processing in 35772 inlines the load. `SaveLoadManager.cpp` hooks both entry points and asks
`SaveService::OnLoadRequested`, passing the filename selected by the engine. For quickload, 35789 retrieves the
current character's `SaveEntry::lastQuicksave` (CommonLibSSE-NG, checked offset `0x40`):

- Offline: let the engine load normally.
- Guest or joining: refuse and raise `SessionNoticeEvent`, which the chat displays. A refused menu load force-hides
  its journal and loading fade: Skyrim already began the fade before calling `Load_Impl`.
- Host: read the selected save and its co-save through `IJoinBridge::ReadSave`, then defer to
  `HostService::UpdateReload`. An unreadable save is reported immediately, before the session is held.

The remaining path is shared with [the wipe reload](death.md#wipe-reload):

1. `HostAuthority::SendCheckpoint` holds gameplay requests and world updates; only the transfer runs.
2. `NotifyPartyReload` precedes the selected save's chunks for every guest, including games already receiving a
   join save.
3. Guests receive the save through `JoinService`, close their connections and load it.
4. Once every guest has left to load, the host loads the same bytes under the existing `SkyrimCoopWipe` scratch
   name. Its authority is rebuilt on the same listener. Authentication requests received during the load wait.
5. Each guest applies its kept `PlayerRecord`, including worldspace, cell and position, then rejoins.

A wipe still selects `PreviousSave()`. A host-initiated load selects exactly the requested file, even when a newer
save exists. Repeated input during an active transfer or load does not replace the chosen save. Once the load
finishes, another host load follows the same path.

`BGSSaveLoadManager::Load` is the internal bridge entry and calls the original `Load_Impl` trampoline directly.
This permits authorized join and party loads without treating a user-selected filename as permission: a guest's
own `load SkyrimCoopJoin` is refused too. The gate adds no messages or opcodes.

A missing chosen save leaves the session running and lets the host choose again. An engine load failure uses the
wipe's failure path: chat on the host and a failed authentication for waiting rejoins, including guests whose
reconnect arrives after the failure. A later valid choice starts a new reload; refused guests join with its save. Guests absent from an older
save retain the join flow's existing character fallback.

## Hook evidence

Checked against `reference/CommonLibSSE-NG/include/RE/B/BGSSaveLoadManager.h` and its `.cpp`, and disassembled
1.6.1170 `Load_Impl` at `0x140610480`: manager in RCX, filename in RDX, device in R8D, output statistics in R9D,
check-for-mods on the stack, boolean result. Internal loads keep `Load_Impl(name, -1, 0, true)`.

Quickload is separate: `QuickSaveLoadHandler::ProcessButton` (52251, `0x14094AA60`) queues `0xD0000400` through
35769 at `0x140612D70`, whose signature is `void(BGSSaveLoadManager*, uint32_t)`. `Thread::ProcessEvents_Internal`
(35772, `0x1406130B0`) handles it at `0x1406132DB`, retrieves the character's `SaveEntry` through 35789
(`0x140617390`, signature `SaveEntry*(BGSSaveLoadManager*, const uint32_t*)`) and loads `lastQuicksave` directly
through the save utility. This differs from `LoadMostRecentSaveGame` (35766), which has no direct call sites in
1.6.1170. Intercepting the queued request captures the exact quicksave before any world teardown begins.

The first Skyrim quickload scenario caught the bypass: only the host actually loaded, while its clock made both
games look restored. The scenario now changes the guest's level after saving and requires it to roll back too.

## Verification

`Code/tests/games/host_load_flows.cpp` covers a selected quicksave or menu save older than the newest save, three
guests restored as distinct saved characters at distinct positions, the same listener retained, two consecutive
loads, guest refusal (including internal-looking filenames), refusal while joining, authorized join loads,
offline loading after leaving, recovery from a missing chosen save, and reconnect refusal after an engine load
failure followed by a successful retry. Existing `[death]` flows cover the shared
wipe path.

Verified 2026-10-26: `scripts/check.sh` passes (all native tests, UI fixtures and the Windows plugin build).
All five Skyrim scenarios pass in one run: `host-loads-party`, `host-quickloads-party`, `host-menu-loads-party`,
`guest-cannot-load` (console, F9 and Load menu, with control returned), and `death-wipe-checkpoint`.
Logs: `build/check/host-loads-review-gate.log` and `build/check/host-loads-review-scenarios.log`.

Network simulation limitation observed during verification: under `COOP_NET`, the fake engine advances a fixed
1/60 second on every tight-loop update, while packets use wall time. Reload rejoins can consume their 180 seconds
in under a second of real traffic and fail as unreachable. Existing death flows also time out with the same
profile; the new load flows pass without the synthetic delay. Logs: `build/check/host-loads-network-debug.log`
and `host-loads-network-1.log`. No transport timeout changes are part of this feature.

Deferred after appearance review: a save immediately after a guest changes clothing can keep its previous outfit,
since character records are sent at most every five seconds. Accepted for now; no record-timing change was made.
