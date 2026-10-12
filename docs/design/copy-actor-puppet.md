# Copies as puppets: an actor with a body and no mind

Handoff for the agent doing this work (written 2026-10-11, at commit 550ece84). Read this whole
page, then `CLAUDE.md` (especially "Engineering rules"), `docs/design/copy-contract.md`,
`docs/re/README.md` and the memory index. Ask the user before committing anything; they review
every change.

## The goal

A **copy** is the actor a game shows for an actor another game owns: a remote player's character,
or the client's replica of a host NPC. What a copy needs to be:

- **A body:** 3D, animation graph, collision/Havok, character controller, it can be hit, targeted
  and killed, it shows effects, it holds and wields equipment.
- **No mind:** no packages, combat decisions, weapon draw/sheathe decisions, dialogue, crime,
  detection, reservations, headtracking choices, movement of its own. Its owner's game decides all
  of that; the sync drives position, rotation, graph variables and actions.

In the user's words: "a dummy NPC that has physics / we can animate, but doesn't have any of the AI
logic, so basically the player character." The player character is exactly that (input drives it,
no AI). But there is only one: `g_thePlayer` is read in 2843 places across 1286 engine functions, so
extra `PlayerCharacter`s are not an option (see `copy-contract.md`). Copies are `Character`s
(NPC-bodied actors), and today the engine runs **its NPC AI on them every frame**.

**Get it right, not quick.** The user stopped the previous attempt because it filtered symptoms
(a hook dropping one engine call, a retry loop) without understanding the system; one of those
hacks caused a regression elsewhere. Every hook or flag change in this work needs evidence from
IDA, CommonLibSSE(-NG), existing SKSE mods or documented engine behaviour that it is the right cut.
Hacky logic is acceptable only with evidence that it has to be done that way.

## What is known (evidence, 2026-10-11)

IDs are Address Library IDs for 1.6.1170 (`docs/re/ENGINE_REFERENCE.md`, `scripts/re/relib.py`).
IDA instance: `build/re/SkyrimSE_1.6.1170.i64` (ida-multi-mcp).

### 1. Copies run the engine's per-frame NPC AI

- `Character::sub_37347` (0x1406672B0) is the character's per-frame AI update. Its first checks:
  `boolBits` (Actor+0xE8, our `Actor::flags1`) `& 2` clear → return. That bit is CommonLib's
  `BOOL_BITS::kProcessMe`, which `Actor::EnableAI(false)` clears. Then it evaluates packages,
  favours/dialogue, crime faction, timers, life state, starts the do-nothing package, and calls
  `sub_1406D3C10` (0x1406D3C10), the AIProcess procedure runner.
- `sub_1406D3C10` runs package procedures, `Actor::SetHeading`, `TESObjectREFR::MoveRefToNewSpace`
  (worldspace changes!) and calls `sub_1406F20A0`.
- `sub_1406F20A0` (0x1406F20A0, size 0x220C) contains the **weapon draw decision**: for a
  non-player actor with hands up (weapon state >= 3) it calls `DrawWeaponMagicHands(false)`
  (vtable 0xA6, ID 37279) unless it has a combat target, its package has flag 0x800000 ("weapons
  drawn"), it is alerted, or a high-process flag (+1111/+1132) is set, or it is a flagged teammate
  near a player with drawn weapons. Call site 0x1406F3050 (return address SkyrimSE+0x6F3056).
  A copy has no packages and no combat target (copy contract), so every frame its AI tells it to
  sheathe.
- `Actor::DrawWeaponMagicHands` (37279, 0x140662F10): with `false`, performs the sheathe action
  (`TESActionData::Func5`), then sets weapon state 4 ("wants to sheathe") if that failed, 5 if it
  succeeded; our `HookPerformAction` refuses a copy's own actions, so a copy sits at 4. It also
  writes `MiddleHighProcessData+0x32E` (CommonLib `unk32E`) via `sub_140713080`. That byte is read
  only by AI functions (`sub_140713060` callers: `sub_1406F20A0`, `sub_1406F82A0`,
  `sub_140704480`, `sub_140704580`; `sub_14070F7D0` resets it), **not** by casting.
- `Actor::Update` (37348) at +0x1242 (return SkyrimSE+0x668F82): if weapon state is 1 or 4 it
  calls `DrawWeaponMagicHands(state == 1)` again every frame. That is vanilla's retry of a pending
  request, for every actor; it only follows from the AI decision above.

### 2. The copy contract's "no per-frame AI" row is wrong

`copy-contract.md` says `HookActorProcess` (Actor.cpp, ID 37356) skips the per-frame AI update.
It does not: 37356 is `Actor::ApplyMovement`, called from `Actor::UpdateMovement` (37350) at
+0xFB, under `UpdateActorMovementJob`/`UpdatePlayerMovementJob`. Precision
(`reference/plugins/Precision/src/Hooks.h`, "ApplyMovement") and po3-Tweaks
(`Fixes/ToggleCollision.cpp`, "Actor::UpdateMovement") hook exactly that call. Upstream Tilted
Online hooked the same function since 2020 (SE 0x1405D87F0; relib confirms 37356 on every AE
build), calling it "ActorProcess". So copies skip the engine's movement integration (right: the
sync positions them) but run its AI.

### 3. Vanilla's "no AI" switch has side effects

`Actor::EnableAI(false)` (CommonLib: clears `kProcessMe`; also zeroes the char controller's linear
velocity) makes `sub_37347` return at once. Documented side effects: "Disabling an actor's AI will
also disable its havok processing", the actor freezes; moving it while AI is off moves its collision
but leaves its model behind until AI is enabled again
([CK wiki](https://ck.uesp.net/wiki/EnableAI_-_Actor),
[Fallout CK wiki](https://falloutck.uesp.net/wiki/EnableAI_-_Actor)). Copies are moved every
frame, so `EnableAI(false)` as-is would freeze them visually. Unknown: which engine paths read
`kProcessMe` besides `sub_37347` (Havok, animation update, char controller, ragdoll), and whether
the model/collision split is the 3D update being skipped or something we can drive ourselves.

Also worth checking: `Actor::SetPlayerControls` (Papyrus; makes an NPC take player input, "AI
disabled after setting, call EnableAI afterwards to actually move the NPC"). Vanilla's own
"player-driven NPC" mode might be the closest thing to the puppet we want.

### 4. Replayed actions are refused by the copy's behaviour graph

`AnimationSystem::Update` replays the owner's actions with `ActorMediator::ForceAction`
(Games/Animation.cpp) → `PerformComplexAction` (38953) → paired-animation check (32803, passes for
unpaired actions) → `sub_1407C2FB0` → `IAnimationGraphManagerHolder` vfunc +0x8
(`NotifyAnimationGraph(eventName)`). A refusal means **the copy's graph had no transition for the
event in its current state**. Observed (DIAG traces, since removed):
- right after a draw/equip (`IsEquipping` true) a replayed `MRh_SpellAimedStart` is refused; the
  same event is accepted 66 ms later;
- ~0.9 s after a `MagicForceEquip`, graph idle by the booleans we sampled, it was refused and
  accepted after 213 ms of retries; the next casts were accepted at once;
- NPC copies refuse some locomotion events for good (`moveStop`, `turnStop`, `TurnLeft`, ...),
  probably because the synced graph variables already put them in that state.
- the copy's replays can arrive bunched (a queue of 3-4 after a stall), which shrinks the owner's
  gaps between actions.

Unknown: what state the graph is in when it refuses, whether the per-frame AI (section 1) is part of
why (the refusals happened both with and without the copy at weapon state 4), and whether the engine
exposes "would this event be accepted" (CommonLib `BShkbAnimationGraph`, `hkbStateMachine`,
`BSAnimationGraphManager`).

### 5. What was tried and reverted (do not repeat blindly)

- A hook on `DrawWeaponMagicHands` dropping unforced sheathe requests for copies. Its stated reason
  (the 0x32E byte blocks casts) was wrong (section 1).
- A retry of refused replayed actions for up to 1 s: generalised to every refusal it made NPC copies
  lag (permanently refused locomotion events blocked their queues) and the suite's resets failed;
  narrowed to "drawing or just changed hands" it fixed the cast scenarios but its rule was fitted to
  traces, not derived.
- With those two in, `party-changes-cells` left Riverwood NPCs (Gerdur, Hod, Sven) in the client as
  copies with no owner, 3/3 runs; with only the door fix (550ece84) it does not. Which of the two
  caused it was not established.

## Symptoms this work should fix (scenarios)

Failing at 550ece84 because of copies (see `build/harness-split/baseline.txt` for the full list):
- firebolt-client-hits-host-npc, flames-client-hits-host-npc, client-angers-host-ally,
  burning-shows-on-copy, input-mouse-cast: the host's copy of the client ends at `Magic_Equip` /
  `MagicForceEquip` while its owner is at `MRh_Equipped_Event` / `MRh_SpellRelease_Event`.
- Possibly related (check, don't assume): death-host-npc / client-spell-kill-credit (a dead NPC's
  `animation.state1` differs on its copy), load-door-not-replayed and party-changes-cells (a copy's
  latest animation empty, action 0, after a cell change or respawn), client-melee-kill-credit (the
  second swing doesn't land), late-join-client.

Not caused by copies: input-keyboard-draw (opt-in OS input scenario, fails on the host's own
player).

## The work

1. **Map the system.** In IDA, starting from `Character::sub_37347`, `sub_1406D3C10`,
   `sub_1406F20A0`, `Actor::Update` (37348), `Actor::UpdateMovement` (37350) and the animation update
   jobs (Precision: `RunOneActorAnimationUpdateJob`, 40436/41453), document what runs per frame for
   an NPC, in which job, and what each piece needs: AI decisions vs body (Havok, char controller,
   graph, 3D). Find every reader of `kProcessMe`. Name what you identify in IDA and export it
   (`scripts/re/ida_export.py` → `docs/re/ida_notes.tsv`). Check every name against CommonLibSSE-NG
   and the reference plugins first.
2. **Search for prior art before deciding.** Other projects have puppeted actors: SkyMP
   (github.com/skyrim-multiplayer/skymp, open source) for remote players, Skyrim Together's
   upstream (this repo's history; they synced packages instead), animation/puppet mods
   (Puppeteer Master, NPC control mods), and anything using `SetPlayerControls`, `EnableAI`,
   `SetDontMove` or `SetRestrained` for scripted actors. Add useful sources to
   `scripts/re/fetch_references.sh`.
3. **Decide the cut, with evidence.** Candidates (not exhaustive): `kProcessMe` off plus driving
   whatever Havok/3D update it also stops; one hook at the AI update entry for copies; the vanilla
   player-controlled-NPC mode; something prior art shows. For each, list what a copy loses and
   prove it with IDA or a test. Write the decision and its evidence into `copy-contract.md` (and fix
   its wrong "per-frame AI" row).
4. **Then the replays.** Once copies run no AI, re-measure the graph refusals. If some remain, find
   out from the graph why (state machine state) before choosing between waiting for the graph,
   preserving the owner's action timing, or syncing graph state another way.
5. **Write it up.** `docs/re/systems/actor-ai-and-copies.md` (per-frame NPC update, what we cut and
   why, how a replayed action reaches the graph and why it can be refused).

## Verification

- `scripts/xmake-msvc.sh build -j16 SkyrimTogetherClientDLL`, then `scripts/coop_test.py reload`
  into a running duo (or `stop` + `duo --keep-running`).
- While iterating, run the affected scenarios: `scenario --tag hands,magic,combat --repeat 2`, plus
  `party-changes-cells` and `load-door-not-replayed` (cell changes, respawns).
- Run the lock checker: a copy change can create new lock patterns with our code on the stack
  (`copy-contract.md`, "When syncing a new system").
- At the end, on a fresh duo: `scenario --repeat 3 --shuffle`. Compare against
  `build/harness-split/baseline.txt` (27/38 at 088d78da; 550ece84 adds door-opens-for-both). No
  scenario may get worse; none may end "NOT RUN" because a reset to the baseline failed (that is
  how the reverted hacks showed their regression).
- Temporary diagnostics are fine while investigating (mark them `DIAG`, remove them before handing
  over).
