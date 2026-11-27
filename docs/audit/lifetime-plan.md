# Engine object lifetime: the simple plan

The engine owns every object we touch and frees them on its own schedule. A survey of `dev` `74630b46` found the
code already follows the right rule almost everywhere, and four places that do not. This plan fixes those four,
writes the rule down, and switches on the detector we already have. No new checks, no new types.

```
1 fix the four findings ──► 2 the rule in CODE_GUIDELINES.md ──► 3 heap quarantine on by default in the duo
```

## What the survey found

- Engine pointers kept as fields, statics or container elements outside the engine's own structs: about 20, nearly
  all safe (set and cleared around one call, or objects that live all session). `Code/sync` cannot hold one at all:
  `scripts/check_layout.py` forbids it the engine's types.
- Work that runs later (91 events through `Runner().Trigger`, 33 queued lambdas, 8 threads): every event carries ids;
  one lambda captures an engine pointer that can go away.
- 20 containers keyed by a raw address. An address is a safe key in six situations, and the code uses five:

  | Situation | Example |
  | --- | --- |
  | The object lives all session (code, singletons, registered natives) | `NativeReach`, `CodeReach` |
  | The entry lives only inside one engine call | `FreedWornList` |
  | The whole table is rebuilt from form ids every frame | `s_copyControllers` |
  | Every new keyed object passes our hook, which sets or clears its entry | `ScriptTags::m_calls`, `m_functors` |
  | A hook on the object's destruction removes the entry | none yet |
  | The entry holds a reference that keeps the object alive | `AnimationTrace::m_graphs` |

- The crashes on record are none of the above. They are engine functions whose own lifetime rule we broke
  (`UnequipAll`'s walk, a temporary base recorded, a clip-load callback replaced), found by the heap tools.

## 1. The four findings

```
s_marked (sync/scripts/StoryGifts.cpp)
  before:  Queued(functor, story)  ── story ──► insert        not story ──► nothing
           a functor at a reused address keeps the old one's mark
  after:   Queued(functor, story)  ── story ──► insert        not story ──► erase
```

```
PendingCriminals (sync/crime/CrimeRules.h)
  before:  m_entries[witness address] = {crime address, copy}     erased when judged
           a witness that dies or unloads first leaves its entry
  after:   keyed by the witness's form id, if 39557's hook has the actor (CrimeBridge); else erased on the
           witness's death and unload. Read the hook first.
```

```
FormDebugView.cpp:72
  before:  Queue([pEffect] { pEffect->fElapsedSeconds = ...; })
  after:   Queue([cFormId, cIndex] { find the actor, find the effect, then set })
```

```
s_clipLoads (engine/Havok/GraphStreamHooks.cpp:122): not fixed here
  keyed by a graph manager; entries only added, wiped at 4096. A new manager at an old address inherits the old
  actor's held clip sets, which a sync point then names to copies.
  ──► an entry in docs/bugs.md (Copy-desync follow-ups) with this reading, and the agent task: who destroys a
      graph manager (IDA), remove the entry there; a GameTests flow on the fake's loads. Needs IDA and the duo.
```

Tests: a `UnitTests` case each for `StoryGifts` and `PendingCriminals`: "the keyed object goes and its address is
used again: the new one starts clean". Each fails before its fix. No scenario: neither can be provoked on demand
(`# hand-written` reasoning in the PR).

## 2. The rule

A section "Engine object lifetime" in `CODE_GUIDELINES.md`:

1. Across frames keep a form id or a handle and look the object up when used. Keep a `NiPointer` or
   `BSTSmartPointer` only to hold something alive on purpose.
2. Engine pointers are locals and parameters: not a field, a static, a container element or a capture of queued
   work (`Runner().Queue`, `MainLoopTasks::Queue`, an event through `Runner().Trigger`).
3. A temporary form (id `0xFF......`) is never recorded anywhere that outlives the frame.
4. An address is a key only in one of the six situations above (the table goes in the section).
5. A callback handed to the engine is cancelled before the object behind it goes.
6. Queued work captures by value, never by reference.

Plain text. No gate: if one of these is broken again, that case becomes the check's first test.

## 3. The heap quarantine, on by default in the duo

```
today:  crash ──► someone reruns with SKYRIMCOOP_HEAP_QUARANTINE ──► maybe it shows again
after:  coop_test.py duo boots both games with it; a use after free stops at its first read
```

- Measure first: `coop_test.py profile` with and without `SKYRIMCOOP_HEAP_QUARANTINE=256` (fps, update time,
  memory), numbers in the PR.
- If the cost is acceptable (the scenarios' waits still pass, fps within about 10%): the duo's default, off with one
  flag; `docs/design/testing.md` says so. If not: the weekly combined run only, and the PR says why.
- The history ring (`SKYRIMCOOP_HEAP_HISTORY`) stays opt-in: it costs frames.

## Left out, on purpose

- A lifetime check in the gate, wrapper types for the six situations, a sampling guard: about 350 lines to
  maintain against a handful of sites.
- Lifetime contracts on every engine declaration: written when a function bites, as `WornStacks.h` and
  `BaseChange.h` were.
- The off-thread `World::Get()` callers: a separate job (`docs/bugs.md`).

## Background

- Handles, not pointers: https://code.persistent.info/2018/11/handles-are-better-pointers.html
- Chromium `raw_ptr`: https://chromium.googlesource.com/chromium/src/+show/HEAD/base/memory/raw_ptr.md
- GWP-ASan: https://www.chromium.org/Home/chromium-security/articles/gwp-asan
- MWSE object lifetimes: https://mwse.github.io/MWSE/guides/object-lifetimes
- Core Guidelines lifetime profile: https://isocpp.org/files/papers/p1179r0.pdf
