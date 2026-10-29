# V1 playtest TODO

Agreed work before v1 (2026-10-28), including the [upstream review](upstream-review-2026-10-28.md) and
[integration review](v1-integration-review-2026-10-28.md):

- [ ] **Host-reload freeze:** reproduce and fix the freeze after the host loads a save. The corruption reproduced again around saving, before loading; identify its originating write. The animation guards do not resolve it. Verify both games continue playing after restoration, not just that positions and records initially match. Evidence: [session/item report](v1-session-items-2026-10-28.md#reload-freeze-still-unresolved).
- [ ] **Werewolf reversion crash:** reproduce and fix the guest crash when the host returns from werewolf form. Verify the owner's race/graph transition and its copies finish correctly in Skyrim. The animation safety fixes and restoration of owner values after a race change are implemented; repeated native transitions pass. The historical intermittent graph crash remains unproven as fixed. Evidence: [party-script bugs](bugs.md#party-scripts).

- [x] **Animation memory safety:** bounds/type checks and descriptor invalidation when the behavior project changes.
- [x] **Animation graph ownership:** release acquired references on every exit and use the game's deleting destructor correctly.
- [x] **Casting/projectile crash guards:** handle absent actors, actor extensions and launch results without invalid dereferences.
- [x] **Spawn readiness:** hold spawns while their native actor is loading, then apply them once it is ready.
- [x] **Crafted potions and poisons:** carry recipes for player-created items and weapon coatings through inventory, container/drop sharing and character restoration; verify brewing, transfer and save/rejoin in Skyrim. Built 2026-10-28 ([crafted-potions.md](design/crafted-potions.md)): one id per recipe for the session; brewing, copies, drops, chests and coatings pass in Skyrim; the rejoin's potion steps pass, its known cover-plate difference remains.
- [ ] **Adaptive playback:** replace the fixed copy delay with a bounded adaptive buffer, keeping movement, attack animations and ragdolls on the same playback timeline. Verify ordinary movement/combat, brief late updates, teleports and knockdown/recovery.
- [ ] **Permanent abilities after actor reload:** verify that added passive abilities remain effective on player copies after cell unload/reload and rejoin; fix any failure, using the existing community reapplication logic where appropriate. Coordinate with the party-script worktree's spell/perk changes.
- [ ] **Live menus:** check ordinary chest and merchant use while the world keeps running, target movement/death/disappearance, current item information and saving with menus open. Fix demonstrated failures using the relevant Skyrim Souls handling as a reference.
- [ ] **SSE Engine Fixes compatibility:** test the appropriate Engine Fixes build alongside ours through joining, save/load, combat and death/revival. Check overlapping allocation and perk behavior and resolve demonstrated conflicts.
- [ ] **Merged-build Skyrim acceptance:** rerun the affected native scenarios against the combined dev DLL: dialogue, story events, scenes, vampirism, session exits/rejoins, container transfers, crafting knowledge and death/revival. Verify the scene fix with three players. Complete ordinary separate-account Steam and 3–4-player acceptance, including quest/dungeon play and save/rejoin; resolve failures in standard user flows.
- [ ] **Player package and clean install:** produce a reproducible `test_harness=n` package with the required UI/assets/dependencies, verify installation without a source checkout or development paths, and update the README/setup instructions to describe the current SKSE/Steam flow.

Unchecked items remain agreed v1 work. The three library checks require fixing any demonstrated failures; their inclusion does not mean those failures have already been reproduced. Keep the focus on ordinary gameplay and retain the ragdoll imperfections already accepted for v1.

- [x] **Fus Ro Dah two-player regression.** Native scenarios use the real three-word Unrelenting Force shout through normal shout input. They cover host hitting guest and guest hitting host, the victim's own knockdown and horizontal displacement, the copy's knockdown, and recovery of walking control. Keep the checks tolerant of the ragdoll timing/facing imperfections already accepted for v1. A guest hitting another guest belongs in the later three-player acceptance run.

  Requested 2026-10-28. `fus-ro-dah-host-hits-guest` and `fus-ro-dah-guest-hits-host` (tag `v1`) both pass in native Skyrim. Existing friendly-fire spell tests and console-driven knockdown tests do not verify this interaction.

Related work queue: [upstream fixes review, 2026-10-28](upstream-review-2026-10-28.md).
