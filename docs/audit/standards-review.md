# Code review (2026-11-26)

How the code itself is written, at `dev` 3d7635789: its correctness at the network boundary, global state, the
shape of its classes and functions, how it reaches the engine, and how its messages are written. Each finding names
the files, the standard it breaks (mostly the C++ Core Guidelines, "CG"), a fix and a rough size. Docs only.

It builds on PR #134, which laid the code out in layers and feature folders. Repo-level points (CI, licenses, lint
configuration) are listed briefly at the end.

Every finding was read in the code; counts are from `grep`, `lizard` and `scripts/check_layout.py`.
Severity: **High** (crashes or hangs, or invites bugs everywhere), **Medium** (makes change costly or error-prone),
**Low** (local cleanup). Size: **S** under an hour, **M** a day or less, **L** several days.

## What is already good

- **Host rules check who sends what.** `HostCharacters::OnMovement` drops updates for actors the sender does not
  own; `OnAnnounceCharacter` checks the id the join gave; `HostAuthority::Handle` drops everything but a join from
  a game that is not a player.
- **Hooks are thin.** `engine/ActorHooks/Detection.cpp` reads the engine, asks a rule (`DetectionRules::Installed`,
  `ActorStateRules::MakesOwnNoise`) and writes back; the rule has unit tests. This is the pattern to copy.
- **`sync` cannot include the engine**, and the compiler enforces it.
- **Work from the engine's job threads is queued** (`World::GetRunner().Trigger` in `AiStateEvents.cpp`,
  `EffectEvents.cpp`, `HeadEvents.cpp`), not run on the registry.

## 1. Message decoding (the network boundary)

**1.1 High: a crafted `ReferencesMoveRequest` hangs the host.**
`Code/encoding/Messages/ReferencesMoveRequest.cpp:21-27` and its twin `NotifyReferencesMove.cpp:21-23`:
```cpp
const auto count = Serialization::ReadVarInt(aReader);   // 64-bit, from the sender
for (auto i = 0u; i < count; ++i)                         // 32-bit i: never reaches 2^32
    Updates[sessionId].Deserialize(aReader);
```
A count of 2^32 or more loops forever; smaller large counts spin for minutes. Decoding runs in
`HostNetwork::Receive` before `HostAuthority::Handle` checks the sender, so a connection that never joined is
enough.
Standard: CWE-835; CG ES.100 (don't mix signed/unsigned widths), ES.106.
Fix: `uint64_t` index and a cap; a `TPTests` case with the crafted buffer. Size: S.

**1.2 High: sizes from the sender, unbounded.** `ReferenceUpdate::Deserialize`
(`Code/encoding/Structs/ReferenceUpdate.cpp:35`, `ActionEvents.resize(count)`), `Payloads::PartyItems`
(`Payloads.cpp:637`), and `snappy::Uncompress` with no output limit (`Code/network/Server.cpp:362`,
`Client.cpp:456`; snappy sizes its output from the packet's header). A huge size throws `bad_alloc` inside a
`noexcept` decoder, which ends the game (1.4). Other decoders are bounded, each its own way (`kMaxStacks`,
`kMaxChanged`, `kMaxPerQuest`, `std::min(..., 1024)`, `& 0xFFFF` in 168 places). The bound depends on each
author remembering it; nothing enforces it.
Standard: CWE-770/789; CG I.6 (state preconditions).
Fix: one `Serialization::ReadCount(aReader, cMax)` that also fails when the count exceeds the bytes left, used by
every decoder; `snappy::GetUncompressedLength` checked against a cap first. Size: M.

**1.3 Medium: decoding cannot fail.** `RequestMessage::DeserializeRaw` / `NotifyMessage::DeserializeRaw`
(`Code/encoding/Messages/Message.h`) return `void`; `ReadBool`, `ReadFloat`, `ReadString`, `ReadVarInt`
(`Wire/Serialization.cpp`) drop the reader's `false`. A truncated message decodes into zeros and is acted on.
Only `GraphOps::ReadRun`, `WorldState`'s `Read` and `TryReadVarInt` report failure.
Standard: CG I.10 (use results to signal failure), E.27.
Fix: a sticky `Failed()` flag on `Wire::Buffer::Reader`, set by any short read; the factories check it once and
return `nullptr`. No per-message change needed beyond 1.2's counts. Size: S to M.

**1.4 Medium: exceptions are on, banned, and never caught.** `set_exceptions("cxx")`, 0 `catch` blocks in
`sync`/`engine`/`plugin`/`network`, 2,125 `noexcept` in `sync` headers. Any throw escaping a `noexcept` function
terminates. Two decoders throw on purpose: `Code/encoding/Structs/Factions.cpp:39,49`.
Standard: CG E.12; the project's "no exceptions" rule.
Fix: those two become failed reads; 1.2 removes the allocation throws. Size: S.

**1.5 Medium: messages are written twice by hand, plus a third time for equality.** About 100 messages and
structs each have a hand-written `SerializeRaw` and `DeserializeRaw` that must list the same fields in the same
order. 94 `operator==` are also written by hand (e.g. `LockChangeRequest.h:21`), and the round-trip tests rely on
them: a field left out of `==` makes its round trip pass without checking it. 10k lines in `Code/encoding/*.cpp`.
Standard: CG T.1/T.140 (generic code over repetition), C.161; DRY.
Fix: one template per type, `template <class Archive> void Fields(Archive& ar) { ar(Id, CellId, IsLocked,
LockLevel); }`, driven by a writer and a reader archive, and `operator== = default`. Convert as messages are
touched; new messages use it. Size: M for the archives, then per message.

**1.6 Low: message fields without initializers.** 12 fields in `Code/encoding` (`GameId::BaseId/ModId`,
`LockChangeRequest::IsLocked/LockLevel`, `ActivateRequest::ActivatorId`, ...). A message built and sent with a
field forgotten sends whatever was on the stack.
Standard: CG C.48, ES.20.
Fix: `{}` on each. Size: S.

## 2. Global state and lookups

**2.1 High: session id and form id lookups scan every entity.** `Utils::GetFormIdBySessionId` and
`GetSessionIdByFormId` (`Code/sync/core/SessionIds.cpp:31-52`) walk every `FormIdComponent` through
`World::Get()`; there are 194 calls, many inside per-frame loops over copies: `InventoryService::OnUpdate` for
each waiting copy, `AnimationSystem::Update`'s claim lambda for each copy each frame. 26 more places
open-code the same scan (`std::find_if(view.begin(), ...)`, e.g. `InventoryService::OnInventoryChangeEvent`).
That is O(copies × entities) a frame, growing with party size and NPC count. `GetSessionId` also writes
`uint32_t sessionId = -1`.
Standard: CG Per.7 (design for optimisation), ES.46; DRY.
Fix: a two-way index (`entt::dense_map` session id ⇄ entity, form id ⇄ entity) kept by `on_construct` /
`on_destroy` of `LocalComponent`, `RemoteComponent` and `FormIdComponent`, behind the same three functions; then
replace the 26 open-coded scans. Size: M.

**2.2 Medium: a global `World` and two ways to get a dependency.** `World::Get()` (a static pointer,
`Code/sync/game/World.cpp:211`) is called 232 times: 142 in `engine`, 49 in `harness`, 27 in `plugin`, 14 in
`sync`. Services receive some dependencies by constructor (`InventoryService(World&, dispatcher&, transport&,
IActorBridge&, ...)`) and look others up from the registry (`m_world.ctx().at<IPlayerBridge&>()`, 162 times in
`sync`), so a constructor does not show what a service uses. `World` also *is* an `entt::registry` (public
inheritance), so every service can touch every component.
Standard: CG I.2 (avoid non-const globals), I.3 (avoid singletons), R.6; dependency injection.
Fix: in `sync`, pass what is used by constructor and drop `ctx().at` (start with the bridges, mechanical); in
`engine`, hooks reach the game through one `HookContext` set at install rather than `World::Get()`; make the
registry a member, not a base, when the first two are done. Size: M (sync), L (engine).

**2.3 Low: threading is a comment, not a check.** Which code may run off the main thread is written in comments
("on the graphs' threads too" for `GraphStreams`, "Each runs where the engine calls it" in the event files).
`entt::registry` and `entt::dispatcher` are not thread-safe, and nothing stops a hook on a job thread from calling
`World::Get().view<...>()`.
Standard: CG CP.1/CP.2 (assume multi-threading; avoid data races).
Fix: a debug-only main-thread assert in `World::Get()` and the dispatcher's `trigger`, with a separate
`World::Queue()` for job threads. Size: S.

## 3. Shape of classes and functions

**3.1 Medium: `HostCharacters` holds five features' rules.** `Code/sync/characters/HostCharacters.cpp` (786
lines, 45 methods) registers 14 handlers: characters (announce, remove, respawn), mounts (dismount, board,
unboard, the ride claim), magic (active effects), actor state (perks, values, max values), movement, and travel
(enter cell, travel queued, party summon). `mounts/`, `movement/` and `travel/` folders exist for these.
Standard: CG C.1/C.2 (one responsibility); the project's rule 4 (one folder per feature).
Fix: move the rides to `mounts/HostRides`, values and perks to `actorstate/HostValues`, the travel handlers to
`travel/`; `HostCharacters` keeps the character table and exposes `OwnerOf`. Size: M.

**3.2 Medium: `AnimationSystem::Update` is two algorithms and a duplicate.**
`Code/sync/animation/AnimationSystem.cpp:29-323` (251 lines, complexity 79): the graph-stream mode and the action
queue mode run in one function behind `if (streams.OnStream(aFormId))` checks, and the spawn-state block appears
twice with small differences (lines 62-72 and 73-95).
Standard: CG F.2/F.3 (one operation, short functions), ES.3 (don't repeat).
Fix: `TakeSpawnState`, `PlayStream`, `PlayQueue`, `StopPairs`, each with a `UnitTests` case over
`IAnimationBody`'s fake. Size: M.

**3.3 Medium: the host's crime judgement lives in an engine bridge.** `CrimeBridge::Judge`
(`Code/engine/Bridges/CrimeBridge.cpp:873`, 1,021-line file) decides whether a guest's crime stands (ownership
recheck, cell check, fines) with 28 inline engine-function pointers. `CrimeRules` holds only part of it.
Standard: the project's rule 3 ("engine makes no decisions"); CG C.2.
Fix: keep engine reads and writes in the bridge (`ReadCrimeTarget`, `ApplyTheft`), move the decision into
`crime/HostCrime` or `CrimeRules`, with a `HostAuthorityTests` case. Size: M.

**3.4 Low: other large functions.** `TestHarness::Crime` (`harness/TestHarness/CommandsCrime.cpp`, 272 lines,
complexity 82: a sub-command dispatcher), `DebugService::DrawAnimDebugView` (305 lines), `HostService::MakeAuthority`
(161 lines of wiring lambdas), `CharacterService::OnCharacterSpawn` (138). In all, 39 functions are over 100
lines and 56 have a complexity of 25 or more (of 6,686).
Fix: split as each is touched (a table of sub-commands for `Crime`). Size: S each.

## 4. Reaching the engine

**4.1 Medium: engine functions declared at the call site.** 710 `static VersionDbPtr<...> f(id)` declarations sit
inside function bodies across `engine`, `plugin` and `harness`, each with its own hand-written signature. 14
Address Library IDs are declared in two files with signatures written twice, e.g. 37380, 41408, 41354, 41340 in
both `engine/Bridges/CrimeBridge.cpp` and `harness/TestHarness/CommandsCrime.cpp`. CLAUDE.md records three
inherited hooks whose signatures were silently wrong; duplicated declarations are where the next one will hide.
Standard: CG I.4 (precisely typed interfaces), ES.3; the project's "engine access goes through typed structs".
Fix: each engine function declared once, as a method or free function in its struct's file under
`Code/engine/` (e.g. `TESObjectREFR::IsOwnedBy` for 20210), checked by `scripts/test_ida_hook.py`; the harness
calls those. Start with the 14 duplicates. Size: S (duplicates), L (all).

**4.2 Low: magic form ids.** `0x14` (the player reference) appears in about 50 places in `sync`, `engine` and
`plugin`, beside three named constants that mean the same (`GuestRecord::kPlayerForm`, `ScriptTags::kThisPlayer`,
`FakeEngine::kPlayer`); `GetById(0xF)` (gold) and similar 12 more times.
Standard: CG ES.45 (no magic constants).
Fix: one `core/FormIds.h` set (`kPlayerRef`, `kGold`, ...) and replace. Size: S.

**4.3 Low: hook files copy their include block.** After the hook split, each file kept most of `Actor.cpp`'s
includes: `engine/ActorHooks/Detection.cpp` has 70 includes for 121 lines; `Item.cpp` and `Movement.cpp` include
the same header twice. Every edit to any of those headers rebuilds all the hook files.
Standard: include-what-you-use; CG SF.10; the project's rule 14.
Fix: trim with `clang-include-cleaner` (clang 18 ships it) over `compile_commands.json`, one folder at a time.
Size: S per folder.

## 5. Style left over from upstream

**5.1 Low: two styles side by side.** Upstream's `Utils::` free functions (125 uses in `sync`), `__FUNCTION__`
in log lines (69), `spdlog::error("{}: : failed ...")`, open-coded entity scans; next to the newer style (typed
ports, rules, short comments). Ten timers advance by frame delta in `sync` (e.g. `InventoryService`'s
`kRetry`/`kGiveUp`, `JoinService::kRetrySeconds`), against the project's "events over polling".
Fix: none in bulk; replace as files are touched, starting with 2.1, which removes most `Utils::` calls.

**5.2 Low: owning raw pointers.** `UiService::m_pRenderer` (`plugin/Ui/UiService.h:104`), `Net::Packet::m_pData`,
`UiRenderer`'s handles: 13 `new` / 15 `delete` outside the engine's own allocators.
Standard: CG R.11, R.20.
Fix: `std::unique_ptr` / `std::vector`. Size: S.

## Top 10, ranked

1. **1.1** The `ReferencesMoveRequest` loop: `uint64_t` index and cap. (High, S)
2. **1.2** One bounded `ReadCount` for every decoder; cap snappy's output. (High, M)
3. **2.1** A session/form id index instead of entity scans. (High, M)
4. **1.3 / 1.4** Decoders fail instead of decoding zeros or throwing. (Medium, S-M)
5. **4.1** Each engine function declared once in its struct; the 14 duplicates first. (Medium, S then L)
6. **1.5** One `Fields()` per message for read, write and `==`. (Medium, M)
7. **3.1** `HostCharacters`' mount, value and travel rules to their folders. (Medium, M)
8. **3.2** `AnimationSystem::Update` split by mode, the duplicate removed. (Medium, M)
9. **2.2** Constructor injection in `sync` instead of `ctx().at` and `World::Get()`. (Medium, M)
10. **3.3** The crime judgement out of `CrimeBridge` into a host rule. (Medium, M)

## Suggested order of work

1. **Decoder safety (one PR):** 1.1, 1.2, 1.4's two throws, 1.6's initializers, each with a crafted-buffer test.
2. **1.3**, so 1.5's archives can rely on the reader's failure flag; then **1.5** for new messages and those touched.
3. **2.1**, measured with `coop_test.py profile --load wolves=N` before and after.
4. **4.1**'s duplicates and **4.2**: small, and they remove signature risk.
5. As features are worked on: **3.1**, **3.2**, **3.3**, **2.2** in `sync`, **4.3** per folder, **5.x** in passing.

## Repo-level points, in brief

Not about the code, but found on the way:

- No CI (`.github/workflows` is empty); the gate is the local pre-push hook.
- No fuzz target. `RequestMessageFactory::Extract` is a ready libFuzzer entry point, and 1.1 is the kind of bug
  it finds in seconds. No ASan/UBSan build of the native tests.
- Warnings at `set_warnings("all")`, not errors; a clean `GameTests` build shows 33 distinct warnings that
  `check.sh` hides (21 `-Wimplicitly-unsigned-literal`, 7 unused, 1 `-Wreorder-ctor`).
- No `.clang-tidy`; two contradicting `.clang-format` files (root one applies to nothing).
- The player package ships 3 of about 17 linked libraries' licenses and not the Oswald font's (OFL).
- No `SECURITY.md`, `CHANGELOG.md`, SPDX/REUSE, code of conduct or issue templates; `NOTICE` names a
  `code/launcher` that does not exist.
- Python scripts: no `pyproject.toml` or dependency list; ruff finds 119 unused imports and 4 redefinitions.
- 52 of 163 messages have no round trip in `Code/tests/encoding.cpp`.
- Dead: `build/BranchInfo.h` generation, `GameFiles/Skyrim/SkyrimTogether.esp`, `.githooks/commit-msg`.
