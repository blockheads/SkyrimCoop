# Contributing

Thanks for helping. This page is all you need to make a first change.

Read [docs/architecture.md](docs/architecture.md) first. It is one page.

## Set up

You need:

- Linux, xmake, clang 18 and libstdc++ 13
- Skyrim Special Edition on Steam, to run your change in the game
- A reverse engineering tool, to read Skyrim's code. We use IDA; Ghidra or another works too.

```bash
scripts/xmake-linux.sh f        # once: the fast tests
scripts/xmake-msvc.sh f -m dev  # once: the plugin (downloads LLVM and the Windows SDK)
scripts/check.sh                # builds everything and runs the fast tests
```

If that passes, you are ready. Running the plugin in the game is in [docs/design/testing.md](docs/design/testing.md).

The fast tests need no game, so most day-to-day work is quick. The game and the RE tool are for finding out what
Skyrim really does.

## Make a change

1. Branch from `dev`.
2. Write a test that fails, then the change that makes it pass.
3. Run `scripts/check.sh`.
4. Open a pull request to `dev`. Say what changed and how you tested it. CI runs `scripts/check.sh --no-dll` on it
   (`.github/workflows/check.yml`).

## The rules

Rules 1 and 2 are checked by the build and `scripts/check.sh`. Old code still breaks some of the others (rule 3
most of all); new code must not.

**Structure**

1. **Layers point one way:** `plugin -> engine -> sync -> encoding`. Never include upwards.
2. **`sync` never includes an engine header.** It asks for what it needs through a port (a small interface).
3. **`engine` makes no decisions.** It holds Skyrim's structs, hooks and calls. A hook reports what happened.
4. **One folder per feature.** Its service, host rule, guest part, port and tests sit together.
5. **Host code and guest code are separate files** (`Host*`, `Guest*`). No "am I the host?" checks inside them.

**Size**

6. **A file does one job.** When it grows long, it is doing two: split it.
7. **An interface serves one feature.** When it grows long, split it by who uses it.

**Engine code**

8. **Use typed structs, never raw offsets.** Each struct checks its offsets with `static_assert`.
9. **Every hook names its Address Library ID and the engine function it hooks.**
10. **Look before you reverse.** Search GitHub and Nexus for a mod that already does it.

**Tests**

11. **Every bug fix leaves a fast test** (tier 1 or 2) that fails without the fix.
12. **Test code never changes production interfaces.** The test harness reads the game its own way.
13. **Tests wait for state, never for time.** No sleeps, no retries.

**Style**

14. **Include what you use.** Every header compiles on its own.
15. **No exceptions.** Return `bool` or a result value.
16. **Comments are one or two lines:** what the code is for. The reasoning goes in `docs/`.
17. **Use the glossary's words:** host, guest, owner, copy, player. See [the glossary](docs/design/glossary.md).

Naming and formatting are in [CODE_GUIDELINES.md](CODE_GUIDELINES.md).

## Fixing a bug

1. **Understand it.** Find the engine code that causes it. Write what you learn in `docs/re/systems/`.
2. **Recreate it in the game,** as a player would, in a scenario with two Skyrims.
3. **Show it in a fast test** that fails (tier 2: whole games on the fake engine).
4. **Fix it,** with unit tests for the logic.
5. **The fast test passes,** and so does `scripts/check.sh`.

If the fake engine cannot show the bug, teach the fake what Skyrim does first.

The full flow, with the tools, is in [docs/design/testing.md](docs/design/testing.md) and
[docs/re/README.md](docs/re/README.md).

## Where to find things

- [docs/architecture.md](docs/architecture.md): how the code is organised
- [docs/progress.md](docs/progress.md): what works and what is next
- [docs/bugs.md](docs/bugs.md): open bugs
- [docs/design/testing.md](docs/design/testing.md): the test tiers and how to run them
- [docs/design/repro-mcp.md](docs/design/repro-mcp.md): playing a bug in the two games to recreate it
- [docs/design/glossary.md](docs/design/glossary.md): the words (host, guest, owner, copy, player)
- [docs/re/](docs/re/): what we know about Skyrim's engine

A code comment or note may point at a design note under `docs/design`, `docs/research` or `docs/reports`. Those were
removed on 2026-11-26 and read from history: `git show b7b43a3bd:docs/design/<name>.md`.

## License

GPLv3. Your contribution is under the same license.
