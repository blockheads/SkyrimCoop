# Upstream fixes review — 2026-10-28

The best pre-playtest ports are the animation bounds/type checks, animation-manager ownership fixes, casting/projectile null guards, and the actor-loading guard. Crafted potion/poison replication is the clearest ordinary gameplay feature worth bringing over. Adaptive playback is worthwhile but is a separate change to our shared movement/animation/body clock.

This records the source review; no runtime fixes or Skyrim scenarios were part of that review. The animation, graph ownership, casting/projectile and spawn-readiness ports are now implemented, and native Fus Ro Dah scenarios are added. Current completion and validation are tracked in [v1-playtest-todo.md](v1-playtest-todo.md) and the [stability follow-up](v1-session-items-2026-10-28.md#stability-follow-up). `scripts/net_check.sh` was not run.

User decisions, 2026-10-28: animation safety, graph ownership, casting/projectile guards, spawn readiness, crafted potion/poison replication and adaptive playback are all agreed v1 work. Permanent-ability persistence, live-menu behavior and SSE Engine Fixes compatibility must also be checked for v1, with any demonstrated failures fixed. These checks are not claims of reproduced bugs. The checklist is in [v1-playtest-todo.md](v1-playtest-todo.md).

## Baseline and scope

- Our first fork commit is `4eae28f5`, **2025-10-22**. Its upstream base is `7bf195c40b813e06f2bf324b31b44a4df87107d2`, 2025-10-20. `git merge-base HEAD v1.8.3` confirms that base. Having upstream's newer tags locally does **not** mean we merged their fixes.
- Reviewed our `dev` at `e4085691` plus the existing uncommitted v1 session/item fixes. The other agent's `.worktrees/party-scripts-2` remains separate; its dialogue/story/scene/system-quest changes must survive integration.
- Screened the 142 commit entries in the official [base-to-current comparison](https://github.com/tiltedphoques/TiltedEvolution/compare/7bf195c40b813e06f2bf324b31b44a4df87107d2...25a0049a00900b8714e105f795380caf33678012), inspected relevant patches and compared the affected paths locally. Official latest release checked: [1.8.4, 2026-10-22](https://github.com/tiltedphoques/TiltedEvolution/releases/tag/v1.8.4); current `dev`: `25a0049a`, 2026-10-27, including the later waypoint and lock fixes.
- Also reviewed relevant changes/source in the absol89 community fork, CommonLibSSE / CommonLibSSE-NG, Engine Fixes, powerof3's Tweaks, Papyrus Extender, Skyrim Souls RE, Precision, Open Animation Replacer and SkyMP. This is targeted triage of changes affecting our systems, not an exhaustive correctness audit of every library or a claim that all those plugins work with ours.

## Recommended before v1 playtest

### 1. Animation memory safety: port

**Missing in active code.** `Games/Skyrim/TESObjectREFR.cpp`'s `SaveAnimationVariables` reads descriptor indices without checking the actual variable array. `LoadAnimationVariables` checks boolean indices but writes float and integer indices without bounds checks. These paths are called from movement, animation events and spawn handling.

Upstream's [bounds checks, `f609305e`](https://github.com/tiltedphoques/TiltedEvolution/commit/f609305e7824eb413e738ddd167cd6698d67a52d), cover those reads and writes. [Creature re-roll fix #917, `115b5019`](https://github.com/tiltedphoques/TiltedEvolution/pull/917), additionally refreshes the descriptor when its behavior project changes and refuses to write values into Havok variables whose words hold indices/pointers rather than scalar values. Our code still caches `GraphDescriptorHash` until explicitly reset and has neither the project key nor those variable-type checks.

Adapt all three protections together. Our full graph/body work supplements variable replication; it has not removed these calls. This is relevant to vanilla creatures changing their leveled pick and to transformations, not only animation mods.

Fast verification: descriptor/array mismatch, index at end, changed project and non-scalar variable tests. Native verification: different vanilla creature graphs, reload/cell change and beast-form transition. Validate any added Havok fields against our 1.6.1170 executable.

### 2. Animation graph references: port

**Missing.** Both variable functions acquire `BSAnimationGraphManager*`, then have early returns before `Release()`. Our `BSAnimationGraphManager::Release` directly calls its destructor at zero rather than the game's deleting destructor. [Upstream #920, `390071ca`](https://github.com/tiltedphoques/TiltedEvolution/pull/920), gives the references scoped ownership and frees through the game correctly.

Use an engine-compatible owner that releases on every exit; release the graph lock before dropping the final manager reference. Audit other acquisition sites while doing this. The production fix is useful even with F3 untouched; repairing or validating the F3 menu remains outside the user's v1 request.

Fast verification: a counted stand-in proves one release on success and every early exit. Native verification: repeated actor loading/unloading with animation sync. These leaks and unsafe writes are confirmed source findings; neither has been established as the originating write of our separate host-reload map corruption.

### 3. Casting and projectile crash guards: port

**Missing small fixes in code we still use.**

- [Spell caster guard #822, `43252c73`](https://github.com/tiltedphoques/TiltedEvolution/commit/43252c734b3126117fbe1440a90fd38265b15e99): our `ActorMagicCaster.cpp::HookSpellCast` still dereferences `pCasterActor` before checking it. Adapt the guard to our current hook and preserve native behavior for an unsynced cast.
- [Perk hook guard #818, `4f552c5f`](https://github.com/tiltedphoques/TiltedEvolution/commit/4f552c5f61b3637ebf563eab7e12b14bd69a470b): our `MagicTarget.cpp::HookHasPerk` checks the actor but not the nullable result of `GetExtension()`.
- [Community fork projectile fix, `679fa6c4`](https://github.com/absol89/TiltedEvolutionScriptFixes/commit/679fa6c470b30888d071973c3245668b29fdbd26): our `Projectiles/Projectile.cpp::HookLaunch` still asserts that launch returned a resolvable projectile, then unconditionally dereferences it. The separate `Launch` wrapper has guards; this hook does not. A failed native launch should return its native result and skip the outgoing event.

Test the rejected input/result decisions at the fastest useful tier, then ordinary bow, spell and interrupted-cast flows in Skyrim. Check the current native signatures before modifying these hooks; do not blindly copy upstream's older wrappers.

### 4. Spawn during cell loading: adapt with deferred application

**Missing guard, architecture needs adaptation.** [Upstream `5a4a8eae`](https://github.com/tiltedphoques/TiltedEvolution/commit/5a4a8eaec52b9d6cd5b3123471131014136a2b64) tests TESForm's still-loading flag before moving a placed actor. Our `CharacterService::OnCharacterSpawn` checks existence, then applies its base and moves it; our TESForm does not expose the flag. Waiting for 3D later is not equivalent to checking before those operations.

Expose loading readiness through the production actor bridge and hold the spawn until safe. Upstream relies on discovery requesting assignment again; our host owns all NPCs and has different discovery rules, so copying its early return could permanently lose a spawn. A fast flow should prove a held spawn applies once after loading; a native door/cell-change case should prove the NPC appears.

## Additional work and investigation leads

| Candidate | Upstream evidence | Our disposition |
| --- | --- | --- |
| **Crafted potions and crafted poison on weapons** | Community fork [`1480aa5c`](https://github.com/absol89/TiltedEvolutionScriptFixes/commit/1480aa5c9071fb45491afbd2ead7a3fae9d3d4d3), [`1a3d8abf`](https://github.com/absol89/TiltedEvolutionScriptFixes/commit/1a3d8abf4d141116ae1375a4ebffdaf55bcdeab0) and subsequent recipe/ownership fixes | Recommend for the intended vanilla co-op experience. Our inventory entries carry enchantment effects but no potion/poison recipe. `GetInventory` turns created base IDs into zero through `ToSession`; applied created poisons similarly lose their ID. `join-flow.md` explicitly deferred created potions. Reproduce by brewing normally, storing/sharing and saving/rejoining. Adapt the final recipe serialization and created-object lifetime handling to our inventory/container/drop/join paths; a trade UI is not required. Do not copy just the early creation commit without its later fixes. |
| **Adaptive playback / lower perceived delay** | Official [#921](https://github.com/tiltedphoques/TiltedEvolution/pull/921), `23901108`, `0a6784b8`, `a4a33744` | Worth a separate performance change. Current `Services/CopyClock.h` uses **300 ms**; movement, animation and body playback share it. Adapt a bounded adaptive clock across all three, retaining action ordering, teleports and knockdown transitions. Fast clock tests first, then ordinary movement/combat under representative latency. This audit did not run network tests. |
| **Character controller step timing** | [#901, `fbf72883`](https://github.com/tiltedphoques/TiltedEvolution/commit/fbf72883015dba9e26bd1539d3af9d33fa794116) | Strong investigation candidate for sliding/physics instability. Our `ForcePosition` still always updates the controller, and `HookApplyMovement` skips copies without upstream's step refresh. However, our later native movement/upkeep differs. Verify where the controller's step is updated now and reproduce invalid timing before applying the old fix. |
| **Permanent slow-time effects copied to another player** | [`69fe8e67`](https://github.com/tiltedphoques/TiltedEvolution/commit/69fe8e6704789ad0e9c88cd13cd80ef34df20129) | Our `ShouldSync` lacks this exclusion. Our owner-applied effects, per-player scripts and shared `BSTimer` differ from upstream. Test a script-added constant ability being added and removed; do not disable normal Slow Time shouts or shared cinematic slowdown. Upstream's reported example is Enderal, outside vanilla v1. |
| **Null spell/shout entries after deserialization** | [`43bfd173`](https://github.com/tiltedphoques/TiltedEvolution/commit/43bfd173b65f6541f50df15ff634245ce64da398) | Missing compaction in `TESSpellList`. Sensible small defensive fix, but lower priority than vanilla failures: our join refuses mismatched plugin lists, the principal upstream trigger. Coordinate with the other agent's spell-list/copy work. |
| **Unattached exterior cell loading / deferred saved forms** | Community fork [`cabc86bc`](https://github.com/absol89/TiltedEvolutionScriptFixes/commit/cabc86bc6bd19fc2e917595eae49c8dc2a5fca32) | Our `TESWorldSpace::LoadCell` still calls the engine directly; join placement and bridge cell loading use it. Investigate alongside travel/reload, but this patch's own message explicitly says its proposed task-pause/drain behavior was not built or tested in game. Treat it as an RE lead, not a proven ready-to-port fix or a diagnosis of our freeze. |

## Other Skyrim libraries and plugins

These are primarily implementation references. We use our own engine bindings rather than linking CommonLib into the mod, so a CommonLib update does not automatically fix our calls.

| Source and revision checked | Relevant findings | Recommendation |
| --- | --- | --- |
| [CommonLibSSE-NG changelog](https://github.com/alandtse/CommonLibSSE-NG/blob/a6d3ea46/CHANGELOG.md), through `a6d3ea46` / 15.0.1 (2026-10-28) | Recent changes correct ExtraDataList construction/destruction (#424), inventory IDs (#385/#387/#388), skill-advance arguments (#405), HitData member meaning (#374), the empty SpellsLearned event (#438), and BGSScene shutdown field type (#457). Many other changes are VR/1.7-only. | Apply only to matching live bindings after executable verification. Our ButtonEvent already has 0x30 size and correct fields; AddSkillExperience already has advance object/action arguments; container count uses corrected AE ID 16108. Our ExtraDataList creation is manual: compare native constructor 11583/destructor 11584 and ownership at new transfer call sites. Spell/scene bindings need coordination with the other worktree, not a blanket upgrade. |
| [powerof3/CommonLibSSE](https://github.com/powerof3/CommonLibSSE/commits/11a8074a), through `11a8074a` (2026-10-26) | Latest keyword fix handles a missing editor ID in a helper; recent quick-release parameters and 1.7 IDs were corrected. | A helper bug is not automatically ours. We have different callers and a 1.6.1170 target; keep this as the second signature/layout reference, not a feature port. |
| [Engine Fixes](https://github.com/aers/EngineFixesSkyrim64/tree/b289e3de), `b289e3de` (2026-08-29) | Inspected double-perk application, saved Havok data initialization, null-process/hit-data guards, shout equip-event spam and stuck mouse-button fixes. Its memory patch replaces game allocation routines. | Prefer a focused compatibility run with Engine Fixes over embedding the whole patch set. Our perk/copy/allocator hooks can intersect. Exercise actor reload and revive, save/load, shout equip and nested menus. Keep confirmed equivalent fixes local only when our changed behavior needs them. Our existing progress doc already records allocator compatibility as unverified. |
| [powerof3's Tweaks](https://github.com/powerof3/po3-Tweaks/tree/47e8b721), 1.17.1 / `47e8b721` (2026-08-25) | `ReapplySpellsOnLoad.cpp` repairs added permanent spells and no-death-dispel spells on non-player actors. `DistantRefLoadCrash` guards missing 3D. Orphaned-effect cleanup is explicitly experimental. | The permanent-ability behavior is especially relevant to copies standing in for players: add a native ability-survives-copy-unload check when merging the other agent's spell/perk work. Do not run generic NPC cleanup blindly on player copies or import unrelated visual/gameplay tweaks. |
| [Papyrus Extender](https://github.com/powerof3/PapyrusExtenderSSE/commits/14b76e10), `14b76e10` (2026-09-25) | Recent static-hit crash fix (`21119944`) belongs to its extended event implementation; much recent work is new natives and library maintenance. | No direct port identified into our vanilla event path. Use its typed event/hook work when extending our own routing; do not add a dependency solely for this audit. |
| [Skyrim Souls RE](https://github.com/Vermunds/SkyrimSoulsRE/tree/efcea27d), `efcea27d` (2026-10-25) | Unpaused-menu implementation handles target invalidation/death/distance, pickpocket chance changes, item-card updates and deferred save processing. Its latest change adds Enderal menu support. | Our live map already borrows a relevant update fix. Check ordinary live barter/container closure and saving with menus open; port a specific reproduced failure. Do not replace our menu policy or paste its save offsets into 1.6.1170. |
| [Precision](https://github.com/ersh1/Precision/tree/9ef45e0d), `9ef45e0d` (2026-09-01); [Open Animation Replacer](https://github.com/ersh1/OpenAnimationReplacer/tree/f4e7688b), `f4e7688b` (2026-08-31) | Recent commit screening; useful native collision, graph traversal and animation-lifetime references. | No standalone vanilla network fix identified. Their behavior/collision changes are not required to make our existing melee and graph replication work; mod compatibility remains later scope. |
| [SkyMP](https://github.com/skyrim-multiplayer/skymp/commits/2849e676), `2849e676` (2026-10-05) | Latest fixes concern plugin-folder loading and skipping RakNet shutdown to avoid a process-exit hang (#2805); its actor movement/AI approach is already in our references. | RakNet shutdown fix does not apply to our Steam/ENet transport. Actor strategy is an engineering reference, not a reason to replace our tested ownership model. |

## Upstream changes already covered or unsuitable as direct ports

- **ExtraDataList freeze #937:** the important part is already fixed in ours (`ae4b1b86`, 2026-10-11): `BSReadWriteLock` uses native read/write entry points 68233/68239/68234/68240 and `GetByType` unlocks on both exits. Do not count the newer upstream commit as an outstanding fix or as the cause of our different reload hang.
- **Uninitialized spell target #907:** our current `Payloads.h::SpellCast::DesiredTarget{}` is initialized.
- **Quest progression #848 and scene lines #854/#928:** upstream relays party stages and holds scene phases. We replaced that architecture with one host world and routed per-player scripts/scenes. Compare concrete voice/scene behavior during the other worktree's integration; do not bring back the deleted quest relay or patched vanilla scripts.
- **Woodchopping script #841 and Companions induction patch #855:** retain the gameplay cases for native acceptance. Their old script/plugin patches assume upstream's execution model; our player/host routing must be checked first. No proof in this review that these cases currently fail.
- **Ownership epochs, stale assignments, dragon visibility and NPC duplication:** useful historical diagnoses, but upstream's moving ownership and distance-based server spawning are gone. Our host always owns NPCs; guests own only their players. Do not restore the deleted assignment protocol.
- **NPC outfit snapshot repair in the community fork:** it addresses the discovery-to-assignment window. We already send equipment snapshots and populate spawns from host state. Investigate a reproduced missing outfit rather than adding a parallel protocol.
- **Staff summons #894:** our sender's concentration/ward/invisibility filter already rejects a non-SpellItem staff enchantment; upstream's old cast path treated it differently. Keep Sanguine Rose/Staff of the Familiar as a native check before adding extra suppression to the receiver.
- **D3D renderer device fix #829:** our RmlUi and ImGui obtain device/context from the active swap chain; the older upstream CEF overlay is deleted. No direct renderer port selected.
- **KiLoader, launcher path/de-elevation, CEF translations, Docker/server CI, public-party/privacy features, F3 helpers:** obsolete architecture or outside agreed v1 scope. Our SKSE packaging still needs completion, but those launcher fixes are not that work.
- **Community fork trade UI, voice chat, nametags, Campfire/Parkour/RaceMenu addons:** future features. Reuse item recipe handling independently; do not add their larger dependency/API stack just to fix vanilla item sharing.

## Proposed order

1. Animation bounds/types/project invalidation and scoped manager references, then the small casting/projectile guards.
2. Deferred spawn readiness and normal cell-change validation.
3. Reproduce crafted potion/poison retention and sharing, then adapt recipe replication (now agreed for v1).
4. Investigate controller timing and exterior-cell load safety against actual native evidence; keep diagnosing the known host-reload freeze independently.
5. Implement adaptive playback after the safety work and the other worktree's integration.
6. Check permanent abilities across actor reloads, ordinary live-menu behavior and SSE Engine Fixes compatibility, and fix demonstrated failures before v1 playtest.

For fixes, follow the repository's fastest reproducing test tier and native signature/layout checks. These recommendations do not turn every optional compatibility case into a v1 blocker, and do not reopen accepted ragdoll imperfections.
