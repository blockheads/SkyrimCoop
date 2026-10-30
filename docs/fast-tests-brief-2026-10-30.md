# Brief: fast tests that stand in for Skyrim (2026-10-30)

Goal: Skyrim (tier 3) runs only for a new feature or a new engine fact. Everything a scenario checks today is also
checked by a fast test (`ClientTests`, `HostAuthorityTests`, `GameTests`) on a fake engine faithful enough that a
fast pass predicts a Skyrim pass. Read first: `CLAUDE.md`, `docs/design/testing.md`,
`docs/design/test-coverage.md` (the scenario → fast twin map, 138 rows).

```
today                                   target
-----                                   ------
217 scenarios, full suite >10 min       Skyrim: new features, engine facts,
GameTests: 221 cases                      a pre-release pass
Party::Agree: cell, position, dead,     Party::Agree = check_desync's fields
  enabled, copy flag                      (animation, equipment, identity,
check_desync (Skyrim): + animation,       package, pose)
  equipment, identity, package, pose    every "Routine" row in
"Routine" rows: Skyrim is the only        test-coverage.md has a fast twin,
  complete check                          or a stated Skyrim-only reason
```

## Why now

The desync checks added 2026-10-30 found copies whose equipment, idle, name and turn animation differ from their
owner's. `Party::Agree` compares none of those, so tier 2 could not have caught them. The fix for Lydia's armor
needed two engine facts the fake lacked: `EquipManager::Equip` does nothing for an item not carried, and an outfit
is put back on after `removeallitems` with no container event.

## Work, in order

1. **Oracle parity.** Grow `Party::ActorDifferences` (`Code/tests/games/Party.h`) to compare what
   `find_desyncs()` in `scripts/coop_test.py` compares: equipment, identity (name, race), the animation
   action/idle, the running package. Write each field into the fake only as a service already sets it. A new
   difference that shows up in an existing flow is a real bug: report it in `CHAT.md` and don't hide it.
2. **Fidelity: engine facts the fake lacks.** Go through `docs/re/systems/`, `docs/bugs.md` and the past fixes
   that needed Skyrim to show the bug. Each fact gets an entry under "What Skyrim taught the fake" in
   `testing.md`, plus the fake behavior and a flow that would have failed. Where a fact is unknown, the
   harness can measure it once in Skyrim (as with the controls masks).
3. **Twins for the Routine rows.** Take `test-coverage.md` row by row, most-run scenarios first: write the fast
   twin (a shared spec in `Code/tests/specs/` if the steps fit, so one spec drives both runners), then move the row
   to Pre-release. Rows that only Skyrim can show (UI, VM, quest engine) keep a one-line reason.
4. **Speed.**
   - Measure GameTests and the tags first, and update "Today's numbers" in `testing.md`.
   - Then remove the waste: real-time timers in flows (a fake clock), slow fixtures, and scenario waits that
     poll instead of waiting on state.
   - Splitting `coop_test.py` (13.6k lines) into modules is in scope if it makes scenarios faster to write.
     Keep `coop_test.py`'s commands and names.

## Rules

- `CLAUDE.md`'s engineering rules: a test that fails without its fix, generic over per-case, short comments, the
  glossary's words, no harness methods on production interfaces.
- Work in a worktree (`.worktrees/fast-tests`, from dev; see the worktree build notes) and don't commit; the user
  merges. Run `scripts/check.sh` before handing back.
- Another agent fixes the open copy desyncs on dev at the same time (`docs/v1-open-issues-2026-10-29.md`, "New
  NPC desync checks"): `InventoryService`, `References.cpp`, the animation and character services, and new fake
  facts in `FakeEngine.cpp`. Keep `FakeEngine` edits additive. Announce a restructure of `FakeEngine` or
  `Party` in `build/coop-test/CHAT.md` before doing it.
- Skyrim rarely: claim the duo in `build/coop-test/DUO.md` (claim and boot in one `&&` chain), free it after stopping.
  Never run the full suite or `net_check.sh` unasked.
- Done when: `Party::Agree` matches `check_desync`'s fields, every Routine row has a twin or a reason, and the
  numbers before and after are in `testing.md`. A short report goes in `docs/reports/`.
