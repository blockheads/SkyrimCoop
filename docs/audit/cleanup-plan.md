# Cleanup plan

How to act on [standards-review.md](standards-review.md): eight steps, each one or a few pull requests, each ending
with a check in the gate so the problem cannot come back. No step changes behaviour except step 1 (two bugs).

```
1 decoder safety ──► 2 safety nets ──► 3 id index ──► 4 one field list per message
   (bugs, 1 day)      (2 days)          (1 day)          (2 days + per message)
                           │
                           ▼
   5 engine functions declared once ──► 6 classes by feature ──► 7 dependencies ──► 8 small things
      (1 day + per folder)                 (3 PRs, 1 day each)      (2 days + L)       (in passing)
```

Rules for every step:

- One piece at a time, with its test, then delete the old code (no parallel old and new paths left behind).
- **A ratchet for every count.** `scripts/check_layout.py --strict` gets a `must not grow` number for each thing
  being removed (entity scans, `ctx().at`, inline `VersionDbPtr`, ...). New code cannot add to it; each PR lowers it.
- `scripts/check.sh` passes on every PR. Steps 3, 6 and 7 also run their affected scenarios in the duo.

## Step 1: decoder safety

The only step that fixes bugs (review 1.1, 1.2, 1.4, 1.6).

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

Done when: no `ReadVarInt` result sizes a container or bounds a loop (ratchet: 0), and a truncated message is
dropped, not decoded.

## Step 2: safety nets, before any refactor

The later steps move a lot of code. These catch what a move breaks.

```
scripts/check.sh
   ├─ existing: UnitTests, HostAuthorityTests, TPTests, GameTests, layout, RE notes, python tests
   ├─ new: round trip of every opcode (one templated test)
   ├─ new: fuzz smoke, 30 s (both factories)
   └─ new: clang-format on changed lines, ruff, shellcheck
nightly or on demand: the native tests under ASan + UBSan
GitHub Actions: check.sh --no-dll on every pull request to dev
```

```cpp
// Code/tests/fuzz/fuzz_requests.cpp (and fuzz_notifies.cpp)
int LLVMFuzzerTestOneInput(const uint8_t* apData, size_t aSize)
{
    Wire::Buffer::Reader reader(std::span(apData, aSize));
    if (auto pMessage = RequestMessageFactory{}.Extract(reader))
    {   // what decodes must encode and decode to the same thing
        Wire::Buffer buffer; Wire::Buffer::Writer writer(&buffer);
        pMessage->Serialize(writer);
        ...
    }
    return 0;
}
```

Files: `xmake.lua` (options `sanitize` and `fuzz`, Linux only), `Code/tests/fuzz/`, `Code/tests/encoding.cpp`
(`TEMPLATE_TEST_CASE` over the opcode list in `Opcodes.h`: 52 messages have no round trip today),
`.github/workflows/check.yml`, `pyproject.toml` (ruff config, the scripts' dependencies), `scripts/check.sh`.
One `.clang-format` (delete the root one, `SortIncludes: Never` in `Code/.clang-format` so it is safe on edits).

Done when: the gate runs them, and the sanitizer run is clean (fix or list what it finds as bugs in `docs/bugs.md`).

## Step 3: an index for session and form ids

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
Ratchet: entity scans by id (26 → 0).

## Step 4: one field list per message

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
Ratchet: hand-written `DeserializeRaw` bodies must not grow.

## Step 5: each engine function declared once

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
Also here: one set of named form ids (`kPlayerRef = 0x14`, `kGold = 0xF`) replacing about 60 literals.
Ratchet: inline `VersionDbPtr` outside a struct's own `.cpp` (710 → falls per folder).

## Step 6: classes by feature

Review 3.1, 3.2, 3.3. Three independent pull requests.

```
HostCharacters (14 handlers, 786 lines)
   ├─ stays:  announce, remove, respawn, the character table, OwnerOf
   ├─► mounts/HostRides          dismount, board, unboard, the ride claim's rule
   ├─► actorstate/HostValues     values, max values, perks, active effects   (file exists: RelayActorValues)
   ├─► movement/HostMovement     OnMovement
   └─► travel/HostArrivals       enter cell, travel queued, party summon

AnimationSystem::Update (251 lines)
   ├─ StopPairs        ├─ TakeSpawnState (the block written twice becomes one)
   ├─ PlayStream       └─ PlayQueue

CrimeBridge::Judge (engine decides)
   ├─ bridge keeps:  ReadCrimeTarget (engine reads), ApplyCrime (engine writes)
   └─► crime/HostCrime + CrimeRules:  the decision, from plain values
```

Each new `Host*` part takes `const HostCharacters&` for ownership (`OwnerOf`, `IsPlayerCharacter`) and registers
its own handlers with `HostRouter`, as `CONTRIBUTING.md` rule 4 says. The existing `HostAuthorityTests` and
`GameTests` flows move with the code and must pass unchanged; `AnimationSystem`'s parts get a `UnitTests` case
each over the fake `IAnimationBody`.

Duo: mounts and travel scenarios for the first, animation and kill moves for the second, crime for the third.
Ratchet: functions over 150 lines or complexity 50 (a `lizard` step in the gate, today's list as the allowance).

## Step 7: dependencies stated, not looked up

Review 2.2, 2.3.

```
sync:    m_world.ctx().at<IPlayerBridge&>()   ─►  a constructor parameter      (162 sites)
engine:  World::Get() in hooks                ─►  stays for now; gains a debug main-thread assert,
                                                   job threads use World::Queue() (the Runner)
later:   World : entt::registry               ─►  World { entt::registry m_registry; }
```

Order: (a) the assert, which may find real off-thread uses: each is a bug to list; (b) `sync` services take
their bridges by constructor, one feature folder per PR (mechanical; `World`'s wiring grows, services' headers
show what they use); (c) the registry as a member only when (b) is done, since it touches every service.
Ratchet: `ctx().at<` in `sync` (162 → 0), `World::Get()` in `sync` (14 → 0).

## Step 8: small things, in passing or in one sweep

- Hook files' copied include blocks: `clang-include-cleaner` per folder (review 4.3).
- `std::unique_ptr` for the 13 owning raw pointers (5.2).
- Dead code: `BranchInfo.h` generation, `SkyrimTogether.esp`, `.githooks/commit-msg`, `NOTICE`'s launcher line.
- Upstream style (`__FUNCTION__` in logs, `Utils::`) replaced as files are touched; no bulk pass.
- Release needs, before the next public tag: every linked library's license in the package (3 of about 17
  today, and the Oswald font's), `SECURITY.md`, the license version stated (`GPL-3.0-or-later` or `-only`),
  `CHANGELOG.md`.
- The 268 files naming deleted design notes: point at `docs/re/systems/` or drop the reference, per folder.
- Still open from PR #134: 22 `engine → plugin` includes, the four feature cycles in `sync`, the seven
  link-time ports. Add the first two to `check_layout.py --strict` as ratchets.

## What this does not do

- No rewrite of the generated-looking tables (`RTTI.*`, the 65 animation graph descriptors): they are data.
- No bulk reformat, no rename sweep: both make every open branch conflict.
- No change to the sync model, the ports or the layers PR #134 set.

## Sizes

| Step | Work | Needs the duo |
| --- | --- | --- |
| 1 decoder safety | 1 day | no |
| 2 safety nets | 2 days | no |
| 3 id index | 1 day, then scans per PR | a profile run |
| 4 field lists | 2 days, then per message | no |
| 5 engine functions | 1 day for the 14, then per folder | affected scenarios |
| 6 classes by feature | 3 PRs, about a day each | affected scenarios |
| 7 dependencies | 2 days in `sync`; the registry later | affected scenarios |
| 8 small things | in passing | no |

Steps 1 and 2 first and in order. After that 3, 4 and 5 are independent and can go to separate workers; 6 and 7
touch the same services, so one after the other.
