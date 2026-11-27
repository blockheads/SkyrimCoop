# Cleanup plan

How to act on [standards-review.md](standards-review.md). The goal is the least code to maintain, as simple as it
can be while everything still works. So every step must delete more than it adds, fix a real bug, or be a few lines
that find bugs. Work that only adds machinery or moves code around is left out (listed at the end).

```
1 delete ──► 2 bugs and bug-finders ──► 3 simplify ──► 4 tidy
```

How it runs: each piece is done by a worker on its own branch, as a pull request into the integration branch
`agent/cleanup`. Each is reviewed and `scripts/check.sh` run before it is merged there; the affected scenarios run
in the two games on `agent/cleanup` after parts 3 and 4. One pull request from `agent/cleanup` to `dev` ends it.
No behaviour changes except the bug fixes.

## 1. Delete

A survey of what nothing uses, with line counts and the proof for each ([deletion-survey.md](deletion-survey.md)),
then its removal, one commit per kind so each can be reverted alone:

- C++ no code reaches: headers nothing includes, functions and structs nothing references, parked experiments.
  A file with a hook, a Papyrus native or a static registrar is live even when nothing names it: the count of
  installed hooks and registered natives is compared before and after.
- Messages and opcodes nothing sends or handles.
- Upstream leftovers: `BranchInfo.h` generation, `SkyrimTogether.esp`, `.githooks/commit-msg`, `NOTICE`'s launcher
  paragraph, the root `.clang-format`, animation graph descriptors nothing reads.
- Scripts nothing imports or registers.
- Skyrim 1.6.1170 support is measured, not deleted: the owner decides.

## 2. Bugs and bug-finders

### Decoder safety

Review 1.1, 1.2, 1.4, 1.6.

```
packet ─► Server::HandleMessage ─► [snappy: length ≤ cap?] ─► HostNetwork::Receive
                                                                    │
            is the sender a player? ── no ──► decode only AuthenticationRequest
                                                                    │
                                              Factory::Extract ─► DeserializeRaw
                                                                    │ every count through ReadCount
                                              reader.Failed()? ── yes ──► drop, count, log
```

```cpp
// Wire/Serialization.h: the one way to read a count
static bool ReadCount(Buffer::Reader& aReader, uint64_t aMax, uint64_t& aCount) noexcept
{
    // fails on a short read, on aCount > aMax, and when aCount exceeds the bytes left
}

// Wire/Buffer.h: Reader remembers a short read
bool ReadBits(...) { if (past the end) { m_failed = true; return false; } ... }
[[nodiscard]] bool Failed() const noexcept { return m_failed; }

// Messages/RequestMessageFactory.cpp, NotifyMessageFactory.cpp
auto pMessage = s_extractor[opcode](aReader);
return aReader.Failed() ? nullptr : std::move(pMessage);
```

Files: `Wire/Buffer.*`, `Wire/Serialization.*`, both factories, every `resize`/loop sized from the wire (41 sites
listed by `grep -rn "resize(\|reserve(" Code/encoding`), `Structs/Factions.cpp` (the two `throw`s become failed
reads), `network/Server.cpp` and `Client.cpp` (`snappy::GetUncompressedLength` against a cap), `HostNetwork::Receive`
(peek the opcode; non-players may only send a join), `{}` on the 12 uninitialized fields.

Tests: `TPTests` cases built from crafted buffers (count 2^40, truncated at every byte, oversized snappy header);
one `GameTests` flow where `coop_peer` sends the crafted `ReferencesMoveRequest` before joining and the host's
frame still returns.

Done when: no `ReadVarInt` result sizes a container or bounds a loop, and a truncated message is dropped, not
decoded.

### Bug-finders (a few lines each)

- **Sanitizers:** an xmake option `sanitize` adding `-fsanitize=address,undefined` to the Linux test targets.
- **A fuzz target** on both message factories (`Code/tests/fuzz/`, libFuzzer with clang 18): what decodes must
  encode and decode to the same thing.
- **A main-thread check** in `World::Get()`, debug builds only: job threads use the queue (`GetRunner().Trigger`).

What they find is fixed, or listed in `docs/bugs.md`.

## 3. Simplify

### One id lookup

Review 2.1: 194 lookups that scan every entity, 26 more open-coded.

```
before:  GetFormIdBySessionId(id) ─► for every FormIdComponent in World::Get() ... compare
after:   GetFormIdBySessionId(id) ─► ActorIndex::BySession(id)        O(1)

registry.on_construct<LocalComponent/RemoteComponent/FormIdComponent> ─► ActorIndex::Add
registry.on_destroy<...>                                              ─► ActorIndex::Remove
```

```cpp
// sync/core/ActorIndex.h: owned by World, kept by the registry's signals
struct ActorIndex
{
    [[nodiscard]] entt::entity BySession(uint32_t aSessionId) const noexcept;
    [[nodiscard]] entt::entity ByForm(uint32_t aFormId) const noexcept;
    [[nodiscard]] uint32_t FormOf(uint32_t aSessionId) const noexcept;
    [[nodiscard]] std::optional<uint32_t> SessionOf(uint32_t aFormId) const noexcept;
};
```

Order: (a) the index with unit tests, and the three `Utils::` functions call it (behaviour identical, all 194
callers gain); (b) the 26 `find_if` scans call it, a few files per PR; (c) the `Utils::` names become
`ActorIndex` calls as files are touched.

Only 8 places emplace or remove these components, so the signals cover them all. `FormIdComponent::Id` and the
session ids must not be assigned after construction (check: grep shows none outside the constructors; make the
fields `const` to keep it so).

Tests: `UnitTests` for add, remove, a copy held with neither component, an id reused after a respawn.
Measure: `coop_test.py profile --load wolves=N` before and after.

### One field list per message

Review 1.5: read, write and `==` are three hand-written lists per message.

```
            ┌─► Writer archive ─► bytes
Fields(ar) ─┼─► Reader archive ◄─ bytes        (the same order, by construction)
            └─► operator== = default
```

```cpp
struct LockChangeRequest final : RequestMessage
{
    GameId Id; GameId CellId; bool IsLocked{}; uint8_t LockLevel{};

    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Id, CellId, IsLocked, Bits<8>(LockLevel));
    }
    bool operator==(const LockChangeRequest&) const noexcept = default;
};
// Wire/Archive.h: WriteArchive and ReadArchive know bool, varint, Bits<N>, float, string,
// vector (through ReadCount with a stated maximum), optional, and any type with Fields.
```

The bytes on the wire do not change: each converted message keeps its round-trip test and gains a
"same bytes as before" test from a recorded buffer.

Order: the archives and five simple messages; then the `Payloads.h` relayed payloads (one file, most messages);
then the rest as they are touched. Differential messages (`SerializeDifferential`) stay hand-written.

### Each engine function declared once

Review 4.1: 710 inline `static VersionDbPtr<...> f(id)`; 14 IDs declared twice with two signatures.

```
before:  CrimeBridge.cpp:   using TOwns = bool(TESObjectREFR*, Actor*, bool, bool);
                            static VersionDbPtr<TOwns> owns(20210);
         CommandsCrime.cpp: the same id, its signature written again

after:   engine/TESObjectREFR.h:   bool IsOwnedBy(Actor* apActor, bool, bool) noexcept;   // 20210
         engine/TESObjectREFR.cpp: one VersionDbPtr, one signature
         both callers:             pTarget->IsOwnedBy(pPlayer, true, false)
```

Order: (a) a check in `scripts/re/build_re_data.py` (it already reads every ID) that fails when one ID is
declared in two files: lists the 14; (b) fix those 14; (c) per engine folder, move inline declarations into the
struct's file, checking each signature against CommonLibSSE-NG and IDA as the hook rule already asks, and
recording names in IDA. The harness calls the engine's declarations (it may include engine headers).

### The repeated block in `AnimationSystem::Update`

`Code/sync/animation/AnimationSystem.cpp:62-95` takes a copy's spawn graph state in two near-identical blocks
(on the stream, and off it). One `TakeSpawnState` with a `UnitTests` case over the fake `IAnimationBody`.

## 4. Tidy

- **`HostCharacters` by feature:** its mount rules to `mounts/HostRides`, values, perks and effects to
  `actorstate/HostValues`, movement to `movement/HostMovement`, cell and travel handlers to `travel/`. It keeps
  the character table and `OwnerOf`. The existing tests move with the code and pass unchanged.
- **Warnings as errors:** the 33 warnings of a clean `GameTests` build fixed, then `set_warnings("all", "error")`.
- **ruff with autofix** over `scripts/` (119 unused imports), and a minimal config so it stays clean.
- **CI:** one workflow running `scripts/check.sh --no-dll` on pull requests.
- **Licenses:** every linked library's license in the player package, the Oswald font's included.

## Left out, on purpose

- **Constructor rewiring** (`ctx().at` to constructor parameters): more wiring, no less code.
- **Moving the crime decision out of `CrimeBridge`:** worth doing with the next crime bug; it needs the two games.
- **clang-tidy:** noisy on the engine's deliberate casts.
- **"Must not grow" counts** other than the duplicate engine declaration check.
- **Changelog, issue templates, code of conduct, `SECURITY.md`:** when the repo goes public.
- **No bulk reformat or rename sweep:** every open branch would conflict.
