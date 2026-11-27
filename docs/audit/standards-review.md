# Standards review (2026-11-26)

The code base read against well-known C++, build, test, open-source and security standards, at `dev` 3d7635789
(after PR #134's layout and #135). Docs only: nothing here changes code.

It builds on PR #134 (`agent/codebase-audit`), which laid the code out in layers and feature folders and wrote the
rules into `CONTRIBUTING.md` and `docs/architecture.md`. That work is not repeated; what it left open is listed first.

How it was checked: every finding was read in the code. Numbers come from `scripts/check_layout.py`, `lizard`
(function size and cyclomatic complexity), `ruff` and `shellcheck` (default rules, no project config exists),
`grep`, and a clean rebuild of `GameTests`.

Severity: **High** (crash, legal or security exposure, or a gap that hides bugs), **Medium** (slows work or lets
bugs in), **Low** (hygiene). Size: **S** under an hour, **M** a day or less, **L** several days.

## What PR #134 left open

Still true at 3d7635789:

- **The engine still includes upwards and decides.** `check_layout.py`: 22 includes from `engine` to `plugin`
  (rule 1) and 564 from `engine` to `sync`. Ports and events are allowed, but the tool cannot tell them from
  decisions, so rule 3 is unchecked.
- **Feature cycles in `sync`.** `travel <-> death`, `travel <-> movement`, `movement <-> physics`,
  `characters <-> physics`. Not checked by the gate (`--strict` checks only core and wiring).
- **The seven link-time ports and the whole-`IActorBridge` services** (`docs/architecture.md`, "Not done yet").
- **Services reach each other through `World`**, and `IsHosting()` stays where a role is decided.
- **Dead links to removed notes.** 268 files in `Code/` and `scripts/` name a `docs/design`, `docs/research` or
  `docs/reports` note that was deleted on 2026-11-26 (48 distinct paths, 3 still exist). `CONTRIBUTING.md` explains
  the `git show b7b43a3bd:` workaround; `scripts/release.sh` and `scripts/package.sh` point at the deleted
  `docs/design/launcher.md`.

## 1. Code smells

**1.1 Medium: a few functions carry most of the complexity.**
Where (lizard, NLOC / cyclomatic complexity):
`Code/sync/animation/AnimationSystem.cpp` `AnimationSystem::Update` (251 / 79),
`Code/harness/TestHarness/CommandsCrime.cpp` `TestHarness::Crime` (272 / 82),
`Code/plugin/Debug/Views/AnimDebugView.cpp` `DrawAnimDebugView` (305 / 63),
`Code/sync/game/HostService.cpp` `MakeAuthority` (161 / 20),
`Code/sync/characters/CharacterServiceSpawn.cpp` `OnCharacterSpawn` (138 / 29).
39 functions are over 100 lines and 56 have a complexity of 25 or more (of 6,686).
Standard: C++ Core Guidelines F.2/F.3 (one operation, short functions); `CONTRIBUTING.md` rule 6.
Fix: split `AnimationSystem::Update` by phase (gather, decide, apply) with a `UnitTests` case per phase; cut
`TestHarness::Crime` into one function per sub-command (it is a dispatcher). Size: M each.

**1.2 Low: giant files that are data or upstream.**
`Code/engine/RTTI.h` / `RTTI.cpp` (5.6k lines each, RTTI address tables) and 65
`Code/encoding/Structs/Skyrim/AnimationGraphDescriptor_*.cpp` files (3.9k lines of constructor tables) are data
written as code. `Code/harness/TestHarness/EngineCalls.cpp` (2.8k) is hand-written and the largest real file.
Standard: CG SF.1 / "a file does one job" (rule 6).
Fix: generate the RTTI and descriptor tables from a data file (or mark them generated so reviewers skip them);
cut `EngineCalls.cpp` by topic the way `FakeEngine.cpp` was. Size: M (EngineCalls), L (generation).

**1.3 Low: dead code and dead config.**
- `xmake.lua`'s `before_build` writes `build/BranchInfo.h` (`IS_MASTER`, `IS_BRANCH_BETA` for upstream's
  `bluedove` branch) on every build; nothing includes it.
- `GameFiles/Skyrim/SkyrimTogether.esp`: upstream's plugin, referenced by no code, script or doc.
- `.githooks/commit-msg` demands Conventional Commits, but `core.hooksPath` is `scripts/hooks`, so it never runs;
  0 of the last 200 commit subjects follow it.
- `NOTICE` names an LGPL `code/launcher` that does not exist.
Standard: CG "don't keep dead code"; repo hygiene.
Fix: delete all four (and the `NOTICE` paragraph). Size: S.

## 2. C++ practice

**2.1 High: exceptions are on, banned, and never caught.** `xmake.lua` sets `set_exceptions("cxx")`; the rules say
"no exceptions"; there are 0 `catch` blocks in `sync`, `engine`, `plugin` and `network`, and 2,125 `noexcept`
declarations in `sync` headers alone. Any throw (`std::bad_alloc` from a `resize`, `std::length_error`,
`std::bad_function_call`, nlohmann's `type_error`) reaching a `noexcept` frame calls `std::terminate` and takes the
game down. Two deserializers throw on purpose: `Code/encoding/Structs/Factions.cpp:39,49`
(`throw std::runtime_error`).
Standard: CG E.12 (`noexcept` when a throw is impossible or unacceptable), E.25; the project's own rule 15.
Fix: replace the two throws with a failed read; bound every allocation sized from the network (2.2, 7.1);
optionally `-fno-exceptions` for `SkyrimCoopSync` and `SkyrimEncoding` once the standard library's throwing paths
are avoided. Size: S (throws), M (audit), L (no-exceptions build).

**2.2 Medium: deserializers cannot fail.** `RequestMessage::DeserializeRaw` and `NotifyMessage::DeserializeRaw`
(`Code/encoding/Messages/Message.h`) return `void`; `Serialization::ReadBool`, `ReadFloat`, `ReadString` and
`ReadVarInt` (`Code/encoding/Wire/Serialization.cpp`) drop the reader's `false`. A truncated or corrupt message
decodes into default fields and is handled as real. Only a few readers (`GraphOps::ReadRun`, `WorldState`'s
`Read`, `TryReadVarInt`) return `bool`.
Standard: CG I.10 / E.27 (signal failure, don't ignore it); CERT ERR33-C.
Fix: `[[nodiscard]] bool DeserializeRaw`, a sticky error flag on `Wire::Buffer::Reader` (`Failed()`) checked once
by the factories, which return `nullptr` on failure. Size: M (mechanical over ~100 messages).

**2.3 Low: owning raw pointers.** `UiService::m_pRenderer` (`Code/plugin/Ui/UiService.h:104`, `delete` in the
destructor), `Net::Packet::m_pData` (`Code/network/Packet.cpp:17`), `UiRenderer`'s geometry/texture handles,
`Papyrus/GlobalCall.cpp:102`. 13 `new` / 15 `delete` in our own code (the engine's own allocators excluded).
Standard: CG R.11 (no naked new/delete), R.20 (`unique_ptr` owns).
Fix: `std::unique_ptr` (with a deleter for the RmlUi handles), `std::vector<char>` in `Packet`. Size: S.

**2.4 Low: header hygiene is a rule but unchecked.** Rule 14 ("every header compiles on its own") has no check;
`Code/engine/ClientPch.h` and `Code/sync/SyncPch.h` are forced into every file, so a header missing an include
still builds. `Code/encoding/EncodingPch.h` lacks `#pragma once`; `Code/harness/TestHarness/EngineCalls.h:101` has
`using namespace Diagnostics::EngineReads;` in a header, and both PCHs export `using namespace
std::chrono_literals`.
Standard: CG SF.7 (no `using namespace` in a header), SF.11 (self-contained headers); include-what-you-use.
Fix: a gate step compiling each `sync`/`encoding` header alone (no PCH) on Linux, and clang-tidy's
`misc-include-cleaner` (2.5). Size: M.

**2.5 Medium: no static analysis.** No `.clang-tidy` anywhere; `clang-tidy-18` is installed. Raw-cast density is
high where it need not be: 445 `reinterpret_cast`, 79 C-style pointer casts, 29 `const_cast` outside
`Code/external` (most are legitimately in `engine`).
Standard: CG Profiles (type, bounds, lifetime); clang-tidy `cppcoreguidelines-*`, `bugprone-*`.
Fix: a `.clang-tidy` with `bugprone-*`, `performance-*`, `misc-include-cleaner`, `cppcoreguidelines-pro-type-cstyle-cast`
and `-owning-memory`, run on `Code/sync`, `Code/encoding`, `Code/network` over `build/compile_commands.json`; warn
first, then gate on new code only. Size: M.

## 3. Build and repo hygiene

**3.1 High: no CI.** No `.github/workflows` (the API reports 0 workflows). The only gate is the local pre-push
hook (`scripts/hooks/pre-push`), which a contributor without the hook, or with `--no-verify`, skips. External PRs
arrive unchecked.
Standard: OpenSSF Scorecard "CI-Tests", "Branch-Protection"; GitHub flow.
Fix: a GitHub Actions job on Ubuntu 22.04 running `scripts/check.sh --no-dll` (native tests, layout, RE notes,
Python unit tests), with xmake's package cache; branch protection on `dev` requiring it. The DLL build can follow
(xwin download, cached). Size: M.

**3.2 Medium: warnings are neither strict nor errors.** `set_warnings("all")` only; no `-Wextra`, no
`-Werror`; the plugin silences `-Winvalid-offsetof` and `-Wmicrosoft-include`. A clean rebuild of `GameTests`
shows 33 distinct warnings at today's level that nobody sees, because `check.sh` hides the logs of steps that
pass: 21 `-Wimplicitly-unsigned-literal`, 7 unused variables/functions, one each of `-Wreorder-ctor`,
`-Wnull-conversion` and `-Wimplicit-exception-spec-mismatch`.
Standard: CG "compile cleanly at high warning levels" (P.12 tooling); OpenSSF Compiler Options Hardening Guide.
Fix: add `-Wextra -Wshadow -Wnon-virtual-dtor -Wconversion` (start with `sync`/`encoding`), fix, then
`set_warnings("all", "error")` on those targets. Size: M.

**3.3 Medium: no sanitizers.** No ASan/UBSan build of the native tests. `GameTests` runs the real `World`,
services and decoders on Linux, the ideal place to catch use-after-free, overflow and UB that the game only shows
as a crash under Wine.
Standard: Google/LLVM practice; OpenSSF hardening guide.
Fix: an xmake option `sanitize` adding `-fsanitize=address,undefined -fno-omit-frame-pointer` to the Linux test
targets; a nightly or CI job running `UnitTests`, `TPTests`, `GameTests` with it. Size: S to add, M to clean up.

**3.4 Low: two contradicting `.clang-format` files.** Root `.clang-format` (LLVM base, `ColumnLimit: 300`,
`SortIncludes: Never`) and `Code/.clang-format` (Microsoft base, UTF-8 BOM, sorts includes). Every C++ file is
under `Code/`, so the root file applies to nothing; no format check runs anywhere, which is why CLAUDE.md warns
against running the tool on edited files.
Standard: one source of truth (EditorConfig/clang-format practice).
Fix: delete the root file, strip the BOM, set `SortIncludes: Never` in `Code/.clang-format` so formatting is safe
on any file, and add `git clang-format --diff origin/dev` to the gate (changed lines only). Size: S.

**3.5 Low: the release package is a `dev` build.** `scripts/package.sh` configures `-m dev` (`-O1`, PDB) for the
player's DLL, not `releasedbg`/`release`.
Standard: reproducible, optimised release builds.
Fix: package with `-m releasedbg` (O2 + PDB), checked by one duo run before a tag. Size: S.

**3.6 Low: dated test framework.** `catch2 2.13.9` (Catch2 v2 is in maintenance; v3 is a compiled library with
faster builds and better matchers).
Fix: move to Catch2 v3 when convenient (mostly include changes). Size: M.

## 4. Tests

**4.1 Medium: about 50 messages have no round trip.** CLAUDE.md's "Adding a message" step 5 asks for a round
trip in `Code/tests/encoding.cpp`; 52 of 163 opcodes have none (neither by name nor through their relayed
payload), for example `ReferencesMoveRequest`'s notify twin `NotifyReferencesMove`, `NotifyInventoryChanges`,
`DialogueRequest`, `ProjectileLaunchRequest`, `GraphSyncRequest`, `NotifyActorPerks`.
Standard: test-per-message contract (the project's own); coverage of the trust boundary.
Fix: one templated `TEMPLATE_TEST_CASE` over every opcode in `Opcodes.h` (default-constructed and one filled
instance), so a new message is covered without writing a test. Size: M.

**4.2 Medium: no coverage measurement.** Nothing measures what the 906 test cases reach. Gaps are found by
playtests instead.
Standard: common OSS practice (llvm-cov / Codecov).
Fix: an xmake option adding `-fprofile-instr-generate -fcoverage-mapping`, and a script printing per-folder
coverage of `Code/sync` and `Code/encoding`; no threshold at first. Size: S.

**4.3 Low: test names.** `TPTests` is upstream's "Tilted Phoques" name for the encoding tests; unit tests are
`*_tests.cpp`, game tests `*_flows.cpp`, host tests `host_authority/*_tests.cpp`. Fine once known, but the target
names do not say what they test.
Fix: rename `TPTests` to `EncodingTests` (xmake, `check.sh`, docs). Size: S.

## 5. Docs and contributor norms

Present: `README.md`, `CONTRIBUTING.md` (good: one page, rules, set-up), `LICENSE` (full GPLv3), `NOTICE`, a PR
template, `docs/architecture.md`. Missing or off:

**5.1 High: third-party notices are incomplete in the player's package.** `scripts/package.sh` ships the licenses
of PicoSHA2, zstd and lz4 only. The DLL statically links (xmake `add_requires`): spdlog, EnTT, glm, rpmalloc,
snappy, enet6, libuv, nlohmann_json, zlib, MinHook, xbyak, Zydis, RmlUi with FreeType, imgui, magic_enum, mem
(and Tracy when `--profiler=y`). MIT, BSD-2/3, zlib and the FreeType License all require their notice to travel
with binary copies; FreeType also asks for a credit in the docs. The UI ships `Oswald-Variable.ttf` (SIL OFL)
without its license; only Liberation Sans has one.
Standard: each library's license terms; GPLv3 section 4/6 (keep notices intact); SIL OFL condition 2.
Fix: a `THIRD_PARTY_NOTICES.md` (generated from xmake's package manifests plus `Code/external/*/LICENSE`) shipped
by `package.sh`, and `package_check.py` failing when a linked package lacks a notice. Size: M.

**5.2 Medium: no SPDX headers, no REUSE compliance, license version unclear.** 0 files carry
`SPDX-License-Identifier`; 6 carry any copyright line. `README.md` says "GPLv3"; upstream's notice says "version 3
or any later version". Which one a contributor grants is not written down.
Standard: REUSE 3.x (fsfe.org), SPDX; GPL "How to Apply These Terms".
Fix: decide `GPL-3.0-or-later` (upstream's) or `-only`; add a `REUSE.toml` covering `Code/**`, `scripts/**`,
`docs/**` in one place (no per-file churn) with exceptions for `Code/external`; state it in `README.md` and
`CONTRIBUTING.md`. Size: S.

**5.3 Medium: no SECURITY.md.** The mod opens a UDP port on every interface (`Code/network/Server.cpp`,
`enet_host_create` on "any") and decodes what arrives (7.1). There is no way to report a vulnerability privately.
Standard: OpenSSF Scorecard "Security-Policy"; GitHub private vulnerability reporting.
Fix: `SECURITY.md` (supported version: latest release; report via GitHub private advisories); enable private
reporting in the repo settings. Size: S.

**5.4 Low: no CHANGELOG, versioning unstated.** 36 tags up to `v1.8.3` and `scripts/release.sh` making GitHub
prereleases, but no `CHANGELOG.md` and no statement of what a version number means (the Skyrim runtime is also
in the package name).
Standard: Keep a Changelog 1.1; Semantic Versioning 2.0.
Fix: `CHANGELOG.md` with an `Unreleased` section, appended per PR (the PR template can ask); `release.sh` copies
the section into the release notes. Size: S.

**5.5 Low: no code of conduct, no issue templates.** Only `.github/pull_request_template.md` exists; GitHub's
community profile flags both.
Standard: Contributor Covenant 2.1; GitHub community health files.
Fix: `CODE_OF_CONDUCT.md` (Contributor Covenant), `.github/ISSUE_TEMPLATE/bug.yml` (game version, mod list,
logs, `coop_report.py` bundle) and `feature.yml`. Size: S.

**5.6 Low: the PR template is for internal agents.** It asks every contributor for IDA reads, `docs/re/journal/`
and the skyrim-coop MCP. `AGENTS.md` copies CLAUDE.md's bug-fix flow but has already drifted (no step 0).
Fix: a short public template (what, why, how tested, bug-fix flow for bugs) with the RE section marked "if you
touched engine code"; make `AGENTS.md` a pointer to CLAUDE.md with no copied rules. Size: S.

## 6. Scripts

248 Python files (44k lines) and 13 shell scripts.

**6.1 Medium: Python has no lint, types or dependency list.** No `pyproject.toml`, no ruff/flake8/mypy config,
no `requirements.txt`, though scripts import `mcp`, `lz4` and `PIL`. ruff with default rules finds 119 unused
imports, 4 redefinitions (`scripts/coop_harness/registrations.py:5`, `runtime.py:492`, `scenarios/combat.py:602`,
`scenarios/world.py:25`), 7 unused variables, 29 blind `except`, 40 `subprocess.run` without `check`. 180 B023
(closure over a loop variable) are mostly lambdas passed straight to `wait_for` and so harmless, but the rule
cannot tell. 62 of 2,909 functions have type hints.
Standard: PEP 8 / PEP 621 (`pyproject.toml`), PEP 484 typing; ruff/mypy practice.
Fix: a `pyproject.toml` with `[project] dependencies`, a ruff config (`E`, `F`, `I`, `B` with B023 off), run
`ruff check --fix` once, add `ruff check scripts` to `check.sh`; mypy on `coop_harness/core` later. Size: S
(config + autofix), M (the rest).

**6.2 Low: shell scripts are almost clean.** shellcheck: two SC2164 (`cd` without `|| exit`) in
`scripts/check.sh:10` and `scripts/net_check.sh:9`, a few style notes. `scripts/package.sh` is a bash wrapper
around an inline Python heredoc of most of its logic.
Standard: ShellCheck; Google Shell Style Guide.
Fix: fix the two, add `shellcheck scripts/*.sh scripts/hooks/*` to the gate; move `package.sh`'s Python into
`scripts/package.py`. Size: S.

## 7. Security

**7.1 High: one crafted packet can hang the host.** `ReferencesMoveRequest::DeserializeRaw`
(`Code/encoding/Messages/ReferencesMoveRequest.cpp:21-27`) reads `count` as a 64-bit varint and loops
`for (auto i = 0u; i < count; ++i)`: `i` is 32-bit, so any `count` of 2^32 or more never ends, and smaller large
counts spin for minutes; each turn inserts into a `std::map`. Decoding happens in `HostNetwork::Receive`
(`Code/sync/core/HostNetwork.cpp:53`) before `HostAuthority::Handle` checks that the sender is a player, so a
connection that never authenticated is enough. Its notify twin decodes the same way on guests.
Unbounded allocations sized by the sender: `ReferenceUpdate::Deserialize` (`Code/encoding/Structs/ReferenceUpdate.cpp:35`,
`ActionEvents.resize(count)`), `Payloads::PartyItems` (`Code/encoding/Messages/Payloads.cpp:637`), and the
masks up to 65,535 elements in `PlayerRecord.cpp` and `PlayerOwnState.cpp`; an oversized resize throws inside a
`noexcept` decoder, which terminates the game (2.1). Others are bounded case by case (`kMaxStacks`, `kMaxChanged`,
`kMaxPerQuest`, `min(..., 1024)`), so the bound is a convention, not a mechanism.
Standard: CWE-835 (infinite loop), CWE-770/789 (allocation without limits), CWE-20; OWASP input validation.
Fix: cap `count` and use a `uint64_t` index here (S, with a `TPTests` case); then one helper,
`Serialization::ReadCount(aReader, cMax)`, that also fails when `count` exceeds the bytes left, used by every
decoder (M); decode only `AuthenticationRequest` from a connection that is not yet a player (S).

**7.2 High: decompression size comes from the sender.** `Net::Server::HandleCompressedPayload`
(`Code/network/Server.cpp:362`) calls `snappy::Uncompress` with no limit; snappy sizes its output from the length
header in the packet, so a few bytes can ask for gigabytes (to be confirmed against snappy 1.1.10's source, not
vendored here). The same holds on the guest side (`Code/network/Client.cpp:456`).
Standard: CWE-409 (decompression bomb).
Fix: `snappy::GetUncompressedLength` first and drop anything over a cap (the largest real message plus margin,
e.g. 4 MiB); count drops per connection and kick. Size: S.

**7.3 Medium: the decoders are not fuzzed.** There is no fuzz target. The factories
(`RequestMessageFactory::Extract`, `NotifyMessageFactory::Extract`) take bytes and return a message: an ideal
libFuzzer entry point that builds with the Linux clang 18 already used for tests. 7.1 is the kind of bug it finds
in seconds.
Standard: OpenSSF Scorecard "Fuzzing"; LLVM libFuzzer; OSS-Fuzz practice.
Fix: `Code/tests/fuzz/fuzz_requests.cpp` and `fuzz_notifies.cpp` (`LLVMFuzzerTestOneInput` calling `Extract`,
then `Serialize` and decode again, comparing), an xmake target with `-fsanitize=fuzzer,address,undefined`, a seed
corpus from `encoding.cpp`'s round trips, and a 60-second run in CI. Size: M.

**7.4 Low: no rate or size limits per connection.** `enet_host_create` gets unlimited bandwidth and the default
packet size; the host takes any number of messages from a player per tick.
Standard: CWE-400.
Fix: `enet_host_bandwidth_limit` / `ENetHost::maximumPacketSize` set to the largest real save chunk, and a
per-connection message budget logged when exceeded. Size: S.

## Top 10, ranked

1. **7.1** Cap the `ReferencesMoveRequest` loop; one bounded `ReadCount` for every decoder; decode only joins from
   non-players. (High, S then M)
2. **7.2** Cap snappy's uncompressed length on host and guest. (High, S)
3. **5.1** Ship every linked library's license notice, the OFL font licence included. (High, M)
4. **3.1** CI on GitHub Actions running `check.sh --no-dll`, required on `dev`. (High, M)
5. **2.1 / 2.2** Deserializers fail instead of throwing or decoding garbage; no throw reaches a `noexcept`. (High, M)
6. **7.3** libFuzzer targets for both message factories, with sanitizers, in CI. (Medium, M)
7. **3.3** ASan/UBSan build of the native tests. (Medium, S/M)
8. **4.1** One templated round trip over every opcode. (Medium, M)
9. **5.2 / 5.3** License version decided, `REUSE.toml`, `SECURITY.md`. (Medium, S)
10. **3.2 / 2.5 / 6.1** Stricter warnings as errors, a `.clang-tidy`, ruff and shellcheck in the gate. (Medium, M)

## Suggested order of work

1. **Security fixes first (one PR, a day):** 7.1's loop and the two unbounded resizes, 7.2's cap, the two throws
   in `Factions.cpp`, each with a `TPTests` case built from a crafted buffer.
2. **CI (a day):** 3.1 with the current `check.sh --no-dll`. Everything after this lands checked.
3. **Legal hygiene (a day):** 5.1 notices, 5.2 license decision and `REUSE.toml`, 5.3 `SECURITY.md`, 1.3's stale
   `NOTICE` line. Cheap and it is what a release needs.
4. **Make decoders fail (2.2), then fuzz them (7.3) with sanitizers (3.3)** in CI: the fuzzer will find what 7.1's
   manual pass missed; 4.1's templated round trip comes free with the seed corpus.
5. **Tooling:** 3.4 format config and changed-lines check, 6.1/6.2 linters, 3.2 warnings, 2.5 clang-tidy, warn
   first and gate on new code.
6. **Smells, as features touch them:** 1.1's three largest functions, PR #134's open items (engine-to-plugin
   includes, feature cycles, the dead note links), 1.2 generation.
7. **Contributor polish:** 5.4 changelog, 5.5 conduct and issue templates, 5.6 public PR template, 4.3 rename,
   3.6 Catch2 v3, 3.5 release mode.

## Appendix: measurements

- Our C++ (excluding `Code/external`): 1,715 files, 203k lines. Largest hand-written:
  `harness/TestHarness/EngineCalls.cpp` 2,842, `tests/encoding.cpp` 2,760, `tests/games/FakeEngine.h` 2,295,
  `tests/games/physics_flows.cpp` 2,291, `engine/Actor.cpp` 1,459. Largest in `sync`:
  `characters/HostCharacters.cpp` 786, `animation/GraphStream.cpp` 742, `items/InventoryService.cpp` 691.
- Tests: 906 `TEST_CASE`s; 117 unit-test files beside their code.
- `GameTests` clean rebuild warnings: 33 distinct (21 `-Wimplicitly-unsigned-literal`, 7 unused,
  2 `-Winvalid-offsetof`, 1 each `-Wreorder-ctor`, `-Wnull-conversion`, `-Wimplicit-exception-spec-mismatch`).
