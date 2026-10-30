# AGENTS.md

Instructions for coding agents (Codex and others). The full project guide is [CLAUDE.md](CLAUDE.md): read it first;
everything there applies. The rule below is repeated here because it matters most.

## The bug-fix flow (the most important rule here)

Every bug, in this order:

1. **A coop scenario** (`scripts/coop_test.py`, the duo) that recreates it, as a player would.
2. **A smoke test that fails on it:** a `GameTests` flow (whole games in-process on the fake engine,
   `Code/tests/games/`). If the fake cannot show it, first teach the fake the engine fact Skyrim showed.
   **Then free the duo** (stop your games, release `build/coop-test/DUO.md`): steps 2-4 need no Skyrim. If the smoke
   test cannot show the bug yet, queue for the duo again only to learn the engine fact the fake lacks.
3. **Unit tests** for the logic (`ClientTests`, `HostAuthorityTests`), then the fix.
4. **The smoke test passes**, with `scripts/check.sh`.

Iterate in the fast tiers, not in Skyrim: the duo is shared and slow (one pair of games, minutes a run; claim it in
`build/coop-test/DUO.md`). So the duo is queued only to recreate a bug, write a scenario or build a feature; step 1's
scenarios are rerun together in combined runs (before a push or merge, or at a cadence). Every bug fixed this way leaves
a fast test that keeps catching it. Tiers and the fake engine: [docs/design/testing.md](docs/design/testing.md).
