# Progress

Where SkyrimCoop stands. Update it when a phase, system or known failure changes.
Plans live elsewhere: the migration in `docs/design/host-authority-migration.md`, joining a session
(build, game and mod checks, host-stored characters, arrival) in `docs/design/join-flow.md`, how the
party shares one story (the party is the Dragonborn) in `docs/design/party-model.md`, later ideas in
`docs/ideas.md`, engine findings in `docs/re/`, the words (host, guest, owner, copy, player) in
`docs/design/glossary.md`.

**Goal:** friends play Skyrim together (up to a Steam lobby's 250 since 2026-10-29: `PartySize.h`), the host's game is the only authority (direct
authority), and the Skyrim Together server is gone. Guests own only their own player; the host owns
every NPC and the world.

## Next (2026-11-05)

In order. Each bug follows the bug-fix flow (CLAUDE.md).

1. **Checked in-game actions: finish them.** Built: the slim version (2026-11-02, `checked-actions.md`, "As built": the
   physics scenario runs on it, `guest-holds-into-npc` 10 of 10) and the MCP layer ([repro-mcp.md](design/repro-mcp.md)).
   Left, **plan ready for an agent, not started:** `checked-actions.md`, "Finishing it": `move` with "blocked" in the
   plugin, `push_toward`, `act_wait` instead of polling, freeze per action, then the open-loop sites (physics first); two
   questions for the user at its end.

   Why: open-loop actions silently miss their intent (the carry step in `guest-holds-into-npc` sometimes drops the
   bucket). Audit and plan: `docs/design/checked-actions.md`; notes: `docs/research/sync-and-testing-references.md`.
2. **Exclusive object ownership**, after Fiedler (memory decision 2026-11-02). Built 2026-11-02 (`physics-sync.md`,
   "Exclusive holds", replacing Steered holds): every other game plays a holder's frames keyframed, so nobody else grabs
   or pushes it, and the host takes it back at the release. Sequence numbers were not needed: the host's tick ordering
   and `m_released` drop stale frames. Left:
   - A refused second holder: two players grabbing within one round trip both hold it locally; the host should refuse
     the second, which drops it.
   - Fiedler's authority chain: bodies a held or thrown body knocks stay the host's.
3. **From the 2026-10-31 playtest:**
   - **C2 freezes in our code:** Windows only. Needs a package with the PDB kept, and one trigger replayed on
     Windows (reconnect failure, then the main menu; a guest loading the host's save; party travel).
   - **C3 crash on quit:** fixed on Linux, verify on Windows.
   - **P1 performance:** parked; check in the same Windows session.
   - **S3 a guest's "go to jail":** the jail choice by mouse is still to try (`guest-ivarstead-jail-mouse`; pay fine is
     done; the freeze is C2).
   - **S8 missed kill moves:** likely the engine's own room check at the mark (`docs/re/systems/kill-moves.md`); a `/bug` right after one would confirm it.
   - **S6 remainder:** an NPC's detection reads higher in co-op (51 against 13 alone).
   - Done: S1, S2, S4, S5, S7, S10, C3 on Linux. C1 avoided, since players can't loot each other. S6 matched
     vanilla, S9 didn't reproduce, P2 works as designed.
4. **Open bugs:** everything in [bugs.md](bugs.md), in the user's priority order, one section per agent (triaged against
   dev's full run 2026-11-16). The two bugs found 2026-11-02 are done: `actors-walk-after-arrival` passes since
   2026-11-05 (News), `knockdown-lies-as-host` 4 of 4 on 2026-11-16.
5. **Red flags' leftovers:** the flags themselves are done (2026-11-02: 1, 2, 4, 5 fixed; 3 measured and left: news
   below, `docs/design/red-flags.md`). Left: its cleanup and explainer docs.

## News

2026-11-23: **other players' animations move on every frame** (`agent/smooth-animations`): a copy's graph runs its
owner's updates due by its playback, so with both games' frames jittering apart it ran none in some frames (its pose
held while its body moved on, read in IDA: 63581) and two in the next, 5-23 % of frames on the duo: "snappy, not
interpolated". Now each generate pass's pose is kept and the pose shown is blended between the two kept around a
time 1.5 owner frames behind playback (`PoseTimeline`, graph-input-stream.md, "A pose every frame"), as the engine does
for a far graph. On the duo a copy's feet move as evenly as its owner's: over 50 % off their neighbours' speed in 1.1-1.3 %
of frames (owners 0.4-2.2 %, copies 10-19 % before). Measured with `smoothness`, which now has the watcher face the
copy and the mover in third person. Scenarios `copy-poses-every-frame-host` / `-guest`; GameTests "A walking player's copy
shows a pose that moves on every frame..." (jittered frames, and 60 against 50 fps); ClientTests `[pose-blend]`.

2026-11-23: **a save over Steam travels 5x faster** (`agent/save-load-explained`): Steam paced every P2P connection at
its fixed default of 256 KB/s, so a save went at 120-240 KB/s in both playtests (a save sent ahead took 8-10 s and a
load soon after it sent it again). `SteamBridge` sets the send rate to 4 MB/s (min = max: Steam does not adapt
between them) and a 2 MB send buffer. Slower rates also stalled a transfer: ENet resent packets only queued in
Steam and cut its throttle to zero (counted at each hop: none lost). Two-account duo, a whole 1.38 MB save, direct
and relayed: 7.3-10.2 s before, 1.35-1.59 s after
([network-performance.md](design/network-performance.md#steams-send-rate-2026-11-23)). ClientTests `[steam-pacing]`;
scenario `steam-join-sends-save-fast` (two accounts). Not yet measured on a home line.

2026-11-22: **a guest behind the host sees the host's NPCs once its grid reaches them; a soft leash keeps guests
in the host's grid** (`agent/missing-enemies-guest`). Playtest 2026-11-22 (report 1951-468): the host walked ahead to the wolves past Riverwood, the
guest followed and never saw them. Their spawns waited on the guest for a reference it had not loaded, and the wake-up
hook sat on `QueuedReference::Finish` (13022), which the probe counted on 8 of 927 3D jobs; `QueuedReference::End` (13023)
on all of them (measured with `player_probe`; the held NPC's job ending there is inferred, `actor-ai-and-copies.md`). Both are hooked now. The soft leash
([party-travel.md](design/party-travel.md), "Soft leash"): a guest outdoors out of the host's grid for 2 s gets a 10 s
countdown banner, then party travel to the host; back in time, it stays (`HostLeash`, `GuestLeash`, `NotifyLeash`;
none indoors, on a trip or down; a trip that never lands is sent again). Scene scenarios go home when they end, so
several run on one boot. A player's copy left in a cell that detached on the other game (a jump of 4 cells) kept that cell,
nothing moved it into its new cell and it dropped below high process, unseen: `RunPresence` now re-places a copy whose cell is not
attached (`actor-ai-and-copies.md`, "A copy in a detached cell"). Tests: `HostAuthorityTests [Leash]`,
`GameTests [leash]`, scenarios `host-ahead-npcs-reach-guest`, `guest-leash`, `player-copies-after-setpos`.

2026-11-22: **the lockpicking menu draws its lock again** (`agent/lockpicking-ui`;
[lockpicking.md](re/systems/lockpicking.md)). Since personal menus stopped pausing (2026-10-27) the menu opened with
nothing drawn on the host and guests: `LockpickingMenu::ProcessMessage` (51950) builds the lock only on the main thread,
and an unpaused game's UI runs in `Job_UI` (39042). One kUpdate between frames on the main thread builds it. Scenarios
`host-`/`guest-lockpick-menu-builds-lock` (MCP verb `pick_lock`: the lock built, the pick, both games read it open);
ClientTests `NeedsMainThreadBuild`.

2026-11-22: **a disintegrated player's body is gone on every game until it respawns** (`agent/disintegrate-body`;
[death-disintegrate.md](re/systems/death-disintegrate.md), "What we do"). After the freeze fix the victim's own body
showed again once the ash shader ended (lying, then getting up) while the shooter's copy stayed dissolved. The refused
ash or goo end now hides the player's 3D (`kHidden`) until its revive. Snapshot readout `mod.body_hidden` (shown, not compared: a first-person player's is hidden by its camera); MCP verb
`body_report`; `shout_at_lying_player` checks the hidden body and its return.

2026-11-22: **a shout that disintegrates a player froze its game** (`agent/shout-kill-crash`;
[death-disintegrate.md](re/systems/death-disintegrate.md)). Dragonborn.esm's Unrelenting Force carries a disintegrate
effect; on another player its ash pile script set critical stage `DisintegrateEnd`, the engine took the player's 3D
(37616 -> `Set3D` 12358), and the frame never ended: `Main::Update` waited for its jobs (35409) and a job in
`WaitForJobTask` (69516), both measured; that the job waited on the player's update is inferred. `Actor::SetCriticalStage` (37615) and
`Actor.AttachAshPile` (54655) now refuse this game's player: it dies the normal way, with its body and no pile.
Scenario `shout-kills-lying-guest` (MCP verb `shout_at_lying_player`), GameTests `[friendly-fire]`, ClientTests
`[CriticalStage]`.

2026-11-22: **kill moves: the frozen victim, and every weapon tried** (`agent/kill-moves-polish`;
[kill-moves.md](re/systems/kill-moves.md), "A blow and its clip's end in one update"). A guest's kill move on a person
left its victim standing in the clip's last pose for 1.5 s after the blow on every game, and a second swing then
started another kill move on it. A person's victim clip sends `PairEnd`, `KillMoveEnd` and `KillActor` in one update
(12 of 12 measured; a wolf's does not, so the wolf scenarios never showed it); the kill is a queued task (`QueueKill`
37316) counted a frame later (`KillImpl` 37896, read), so the claimer called the pair done with its victim alive, and
the give-back then waited for an update of a graph our own pose hold had stopped. Now a taken victim is not done
while its kill is queued (`ReplayState::PairDone`, `ActorKillQueued`), and a copy holding its pose takes its owner's
death when heard, not at playback behind a stale queue (`ReplayState::TakeOverdueDeath`). Measured on the duo: the
host's victim dies 0.01 to 0.07 s after its clip's blow (21 of 21), the guest attacker's own 0.08 to 0.35 s (26 kill moves;
1.5 s and more before). New: the MCP verb `kill_move_on` (any weapon, either player, front or behind, with what a
player would see wrong after), scenarios `kill-move-drops-at-blow` (recorded) and `kill-moves-by-weapon` (14 rows:
sword, dagger, greatsword, battleaxe, warhammer, mace, war axe, unarmed, a wolf, both players), GameTests for both
fixes and a table of nine kill moves by either player, ClientTests for the two rules. The fake learned: a clip's
kill is counted a frame after its `KillActor`, a held pose's graph records no update, a dead actor is in no kill move
(37896 clears flags2 `0x4000`, read). On the duo the eight older kill-move scenarios and `kill-move-drops-at-blow`
pass; `kill-moves-by-weapon` paired and dropped 14 of 14 and fails on a dead copy's rest (awake, or 6 to 10 units
off: 4 rows, then 1). Left open (bugs.md): that rest, and the 0.08 to 0.35 s, the host's replay lag.

2026-11-22: **piece F of bugs-refactor-plan.md, the world check (F1, F2)** (`agent/world-objects`;
[piece-f-plan.md](design/piece-f-plan.md)). Containers' change `0x8000000` is the host's leveled roll
(`InventoryChanges__AddLeveledEntries` 16128, read in IDA; a guest skips the roll and is given the host's items), so the
world check masks it with `0x20` when the items agree. Where a placed loose item lies (position, turn, bodies, pose and
its move flags) is each game's own and no longer compared (the user, 2026-11-22: negligible); the physics scenarios
keep their own rest checks. `scripts/test_world_diff.py` covers both. bugs.md: "Clutter flags after a vampire rejoin"
closed, the reference bullets of "After a guest rejoins" and the containers and food of "End comparisons in towns"
and "Riverwood's scenery" gone. Not carried over: `guest-throw-lands-on-host` once could not grab ladle `32AFE`
("holds nothing", 2026-11-22). **F3, the mill log:** an actor sitting in a furniture has the furniture's graph manager
made a dependent of its own (`SetSitSleepState` 39912 -> `AddDependent` 63364), so its graph events drive the
furniture (Hod's load of Riverwood's sawmill); a copy never got that link. The owner's AI state part now carries the
furniture it sits in (from `TESFurnitureEvent`) and the copy's graphs are linked the same way
(`Actor::LinkFurnitureGraphs`). Scenario `copy-worker-drives-furniture` (recorded failing, passes after), GameTests "A
copy working a furniture drives the furniture's own graph as its owner does", TPTests round trip; bugs.md's "Riverwood's scenery drifts apart" closed;
[furniture-graphs.md](re/systems/furniture-graphs.md). The harness's animation trace now follows any reference's
graph. **F4, the cover plates:** read, not fixed, parked as cosmetic (bugs.md's entry): the host's cover goes stale
only on the load right after it quits its session to the main menu; vanilla does not; not in the 60 hook files
switched off. Harness: the vanilla stand-in reads a node (`@node`) and quits to the main menu (`@quit`), the MCP's
`vanilla_start` takes a save, and `quit_to_main_menu` is a verb.

2026-11-22: **the floating body after a death: piece B of bugs-refactor-plan.md, fixed** (`agent/floating-body`;
`docs/re/systems/physics.md`, "A revived player's ragdoll stays in the world"; `docs/design/death.md`, "The ragdoll").
Not the kill move: any death. The revive took the player's ragdoll out of the world from our update, between the
graph's listing for the physics step and the step, whose ragdoll pass (63562) put every body back, dynamic; the
standing graph's rigid-body controls then kept the skeleton on them, so after a trip it stood 2,100 units up and
48,000 across. `DeathService` now ends it through the bridge (`EndPlayerRagdoll`), queued on the engine's task pool
as vanilla ends a ragdoll (36924). `kill-moves-cross` then `host-rides-horse` pass (2 of 2 failed before);
new scenario `revived-guest-stands-after-travel` (`expect` gains `stands`), GameTests "A revived guest's skeleton stays
on its feet after it travels" and the engine fact's flow. Harness: `ragdoll` adds the driver's controllers and output
tracks; `ragdoll_adds` names who put the player's ragdoll in the world, `ragdoll_out` takes it out (at once or queued). bugs.md's entry closed.

2026-11-22: **piece D of bugs-refactor-plan.md, rewritten by its step 0** (`agent/copy-starts-whole`;
`docs/re/systems/actor-state.md`, "What a copy's start leaves out"). Read in IDA: only a draw's own `weaponDraw`
(42824 -> ActorState vfunc 0x15, 38821/38979) writes Drawn; sit state comes with the spawn's graph state (not the
stream's sync point). Measured: a copy put up off its stream (37279, weapon state 2) stayed at 2 once its stream's sync
point restored its graph (its `weaponDraw` lost to the restore: inferred). On its stream a copy not settled as its
owner's is now put right (`PutsHandsRight`'s `aOnStream`, both ways); `late-join-client` 4 of 4 (1 of 4 before),
GameTests "A copy put right before its stream starts ends drawn" and "A copy mid-draw whose owner sheathed before its
stream starts ends sheathed" (the fake's draw now ends at its own event, dropped by a sync point). No `StartPart`: the last action's window and the bow's
`[0, bow]` writer were not found (bugs.md notes); sit state needs a measurement first.

2026-11-22: **a plan for the open bugs after the ten pieces** ([bugs-refactor-plan.md](design/bugs-refactor-plan.md)):
about half of the 56 share five causes. Two refactors (one copy teardown between frames, for the guest's crashes;
"a copy starts whole", for copies made after a draw, a sit or an action), two tools first, the rest one at a time.
Proposed, nothing built.

2026-11-22: **lock checker: hook sizes measured, not written, piece A2 of bugs-refactor-plan.md** (`agent/hook-sizes`;
[lock-order.md](re/systems/lock-order.md)). `LockOrder::IgnoreFrames(&Hook)` takes a pass-through hook's code from its own
unwind entry; the written sizes had drifted both ways (`HookUpdateAI` 0x5F against 0x50, four covering the next
function). A hook without an entry fails every lock audit (`lock_order.unmeasured_hooks`), and an `[ours]` report names
the frame that tagged it. On the duo the alias/list pair now reads engine-only (`party-buys-house-guest` passed); the
Havok/list pairs left were read in IDA (engine-only: they warn, not added to the reviewed list). Closed: "A pass-through hook counted as ours", "The AI/quest lock
pair". ClientTests `[locks]`, a harness unit. `party-buys-house-host` failed on its staging (`place`: no clear spot),
not a lock.

2026-11-22: **a guest's drop onto a pile lies as it fell** (`agent/physics-piles-drop`; `docs/design/physics-sync.md`,
"A drop onto a pile"; `docs/re/systems/physics.md`, "A body landing on a sleeping pile", "A body put into a capsule
creeps out"). A guest's object let go over a pile left the pile 3 to 8 units off the host's in about 1 round in 3 and
creeping for 15 s. What its own bodies' island holds is the guest's from its first move (`GuestHands::OwnIsland`), the
host's rests of that island wait, and a body its player's touch shifts under 2 units is moved all the same
(`OwnNudged`): the host's old rest of one was put back into the standing player's capsule, which pushed it out every
step (62648). Three trials with vanilla baselines (`drop_on_pile`, `drop_wheel_on_wheels`, `drop_on_bottles`), their
scenarios, two GameTests flows, the MCP verb `collision_restless`.

2026-11-22: **the full suite on dev after one-simulator pieces 1-10** (`agent/bugs-audit-suite`): 364 of 423 default
scenarios pass; of bugs.md's 62 entries 3 closed, 3 unclear, 56 still open; 8 new (six guest crashes through a pointer that holds a vtable among them).
Details: "Test suite", 2026-11-22.

2026-11-21: **ragdolls follow the pile rule, scenery is nobody's: piece 10 of one-simulator.md, built**
(`agent/piece-10`; plan `docs/design/piece-10-plan.md`; `docs/re/systems/physics.md`, "A lying ragdoll's layer and
group", "A translating reference"). The world check leaves out critters, fish, their landing markers, references on a
TranslateTo path and plants' sway (`world_state`'s `scripts` and `on_path`). A copy's ragdoll at its owner's rest is
glided there, put there once, put to sleep and left (`RagdollRest`), so it no longer wakes after its first sleep; a lying
copy's ragdoll is in the played collision group; an actor's ragdoll rest is its `kRagdoll` record part, so a copy made
after it (a join, a cell load) lies there too. A guest's grab of a host corpse is the mover's until it rests (it lay
151 units apart before); a hit on a corpse was checked on the duo and needs nothing.

2026-11-22: **a hidden guest's theft judged by who sees the host, fixed** (`agent/theft-detected-wrong`;
`docs/re/systems/crime.md`, "A hidden guest's theft judged by who sees the host"; playtest 2026-11-22). StealAlarm
gives every loaded NPC the crime to judge on its next update (39557, 39631), and that look read the host's player:
`Judge`'s scope covered only StealAlarm's own looks. A guest sneaking behind a closed door was attacked through the
host at the bar by the owner who saw the host (alarm, then 39651's owner assault). Now the bridge remembers which
guest's copy each witness's crime names (`CrimeRules::PendingCriminals`) and runs that look in its scope. Scenario
`guest-hidden-theft-host-seen`; harness `crime_watch` (each NPC's alarm, anger, witness flag and sight over time).

2026-11-21: **held-now state for a joiner, piece 9 of one-simulator.md, built** (`agent/piece-9`;
`docs/re/systems/actor-ai-and-copies.md`, "A held cast's end"). A game joining mid-cutscene, mid-conversation, after a
map marker or while another player channelled a spell got none of it. `HostAuthority::PlayerEntered` now sends from one
list of stores (`JoinerStores`, checked by HostAuthorityTests "Every store answers a joiner"): each game's cutscene still
holding the controls (`HostPlayerNatives` keeps its last `DisablePlayerControls`, marked by its sender with
`RoutedCall::Controls`), the party's map marker, and the conversation under way at its step (`NotifyDialogueOpen::Step`;
the joiner joins by talking). A held cast (concentration, ward, invisibility) is the caster's `kCast` record part, so a
copy made mid-cast starts it; `SpellCastRequest`/`NotifySpellCast` are gone. Cutscene tints and fades are not kept (the
user: controls only).

2026-11-21: **the eye is the host's, piece 8 of one-simulator.md, built** (`agent/piece-8`; `docs/re/systems/stealth.md`,
"The eye is the host's"). A guest's stealth eye was its own game's estimate: its NPC copies judged it, and in a fight it
showed hidden (no combat group of its own; scenario `guest-eye-is-hosts`). Now the host reads its NPCs' detection of each
guest's copy every frame (41408, 46877: what its HUD asks of its own player, which also keeps that state fresh; it was
always stale) and sends that guest a `NotifyStealthEye` when it changes; the guest's reads of its player answer it
(`StealthEye` hooks), and every copy is an unseen witness of this game's player. Bugs closed: "The guest's eye is its own
estimate", "The sneak eye with the other player looking".

2026-11-21: **piece 8's blocker read and fixed: a guest's copy makes its owner's noise** (`agent/piece-8`;
`docs/re/systems/stealth.md`, "Line of sight", "A player's noise"). NPCs searched for a still guest once the host died
("A still guest unseen up close"): no line-of-sight ray was blocked (harness `sight`, MCP `sight_watch`); the copy was
out of the NPC's cone and silent. A player who bumps a character makes iSoundLevelLoud for 5 s (37711, only in the real
player's update), so the host heard its own player and never a guest. `ActorStatePart::Noise` now carries the owner's
noise to its copies (held until the owner's ends; `HookSetActionValue` keeps a copy's own engine out): no search in 5
rounds (5 of 5 before). Scenario `guest-bump-heard-on-host`. On the way: `Actor.h` named +0x11C/+0x120 one field off;
`host_death_watch` toggled god mode (now `god_mode`).

2026-11-21: **the host creates, piece 7 of one-simulator.md, built** (`agent/piece-7`; `docs/re/systems/summons.md`).
A guest's Conjure Familiar showed on neither game: its own engine made one and held it without 3D, and the host dropped
the copy's cast. Now the guest's summon effect runs on its game (portal, timer) with `CreateSummon` (34993) refused,
and the host's game puts it on the guest's copy, so the host's engine makes the one familiar, commanded by the copy;
the spawn carries its commander. A guest's script's `PlaceAtMe` goes to the host's game and waits for the host's
reference (the `kCreate` sink, 13723); script values carry created references by their created id (`CreatedIds`),
and `Enable`/`Disable` on them are the host's (`vampirism-is-each-players`' marker). Found on the duo: a copy spawned while
its owner shows an effect never played it (no loaded event for its 3D; `Copy3DEvent`), and a guest joining with the
host's world had the host's created references twice (its save's and the create row's). Bugs closed: "A guest's
summon", "References a guest's script creates". Scenario `guest-summons-familiar`.

2026-11-20: **piece 6 of one-simulator.md revised: activations stay as they are** (`agent/piece-6`). The RE
(`docs/re/systems/activation.md`, open question 4) found the engine already splits each form's `Activate` by
"this game's player" (screen) and anyone else (world). The host already reruns a guest's activation with its copy,
and its rows put the guest right. A gate sending the world part to the host would fix no bug seen, so it is not built.
Its four bugs are fixed one by one. First, **"Doors the host's NPCs unlock" fixed**: a load door's lock lives on the
door it leads to (`GetLock` 20223, `AddLockChange` 19512 records there), and a change made before a guest joined never
reached it, because the guest asks for its loaded cells and the inside door's cell is not one of them. The host's
cell state now sends the lock with the outside door (`ReferenceStates::LinkedLockRows`). Tests: GameTests "A load
door the host's NPC unlocked while a guest was away is unlocked when it joins again", scenario
`late-join-gets-load-door-lock` (control: fails without the row); harness `door_lock`. Found on the way, not fixed:
harvested flora was not synced (fixed in the review, below). Second, **"The host shows a guest's relief box"
fixed**: a call the host's script routes to a guest's game (`NotifyRoutedCall`) was queued there with no cause, so a
screen or controls call read as the story's and went back to every game; the host showed the mirrored box. The guest
now queues it as its own player's (`GuestEngine::RunPlayerNative`, cause 0x14). Tests: GameTests "A guest's action's
message box shows on its game only" (the fake's guest seam had queued it as the player's all along: now it follows
production), ClientTests; `relief-takes-client-relics` 4 of 4 (0 of 4 before), the guest-moment and story-box
scenarios pass.
Third, **"The host's roll for a guest's loot" fixed**: the roll already agreed (the cell state sends contents); a
guest's Take All of a fresh roll moved nothing because a chest keeps a taken roll item's entry at 0 (16059) and
16108/16214 reads that as one. `GetContainerCount` now counts as CommonLibSSE-NG does (`Games/ContainerCount.h`,
ClientTests); `guest-loots-hosts-roll` 3 of 3 (1 of 3 before), the container scenarios pass
(inventory-changes.md, "A roll's taken item").
Review of PR #87: a picked plant goes out as the host's row (`kHarvested`: `SetHarvestedFlag` 14647 and
`UpdateHarvestModel` 14865); a guest's own linked-lock change is named in its cell request; the own-lock check reads
the lock pointer; a guest's activation reaches the host's game as `HostGame::Activate` (`NotifyActivate`, the
guests' replay and `IFormBridge::ActivateObjectFromActor` deleted); fakes model a roll's 0 entry and take the routed
call's cause from `PartyRules::kRoutedCallCause`.
Cleanups: a guest's object activation runs on the host's game only (`HostAuthority::OnActivate`, no `FromAnyone`);
the host's own activations reach the guests only as rows; the door's open-state compare and the field carrying it
are gone (`ActivateRequest`, `NotifyActivate`, `ActivateObjectFromActor`). GameTests "A lever a guest pulls moves once,
on the host, and no other guest replays it", HostAuthorityTests; 21 object scenarios pass. The lock checker no longer
counts `HookNativeCalled`'s frame as ours (bugs.md, "A pass-through hook counted as ours").

2026-11-20: **passengers sit at once, piece 5 of one-simulator.md, built** (`agent/piece-5`). A player boarding a
horse another player drives asks the host first (`BoardRequest`); the host's yes (`NotifyBoard`, to it alone) seats it
with the engine's own quick get-on: `PutOnMount`, then `Actor::PutActorOnMountQuick`
(38699, vfunc 0xAC: 38704 resume, IDLE `EnterHorseInstant` under `IsEnteringInteractionQuick`, 39004 sit state 3,
`SeatRider`), with no paired clip and nothing for the horse (read in IDA; player-mount.md, "Sitting at once"). Its
copies are seated by its record's Ride part (`RecordKind::kRide`), once the horse is here and its stream reaches that
frame; a joiner gets it on the spawn. Gone: the slot emptying and engine get-on, `SeatsAtHoldEnd`, `SkipsMountEntry`,
the pending boards. `party-shares-a-horse` allows the one pop to the saddle and fails on any `HorseEnter` while a
passenger boards. Tests: GameTests `[passengers]` (quick seat, a refused board, a joiner), HostAuthorityTests
`[passengers]`, the Ride part's round trip.

2026-11-20: **kill moves as claimed pairs, piece 4b of one-simulator.md, built** (`agent/piece-4b`; PRs #73 and #75
folded in). A kill move a game's own actor starts on another game's actor is claimed through the host
(an action claim, `HostClaims`): the first claim on either actor holds. A host NPC is then the claimer's game's for the pair, keeping
the copy's deferred kill so its clip's `KillActor` only counts, and goes back with "killed"; the host's game kills it,
credited to the claim's actor. A refused claim yields: crossed, its blow dropped; an NPC that cannot pair (frozen,
dead), the engine's own plain second swing (42350, read). Every other game pairs its two copies itself. A claimed
pair's forwarded blow waits on the victim's game for the pair (closes "a short winner's blow lands first"). Also: a
copy taken back keeps no deferred kill (no engine path ends a scripted one: read, all 38618 callers). On the duo
`client-kill-move`, `host-kill-move`, `kill-moves-cross` and `frozen-wolf-kill-move` pass (kill-moves.md, "Claimed
pairs"). Left for a player victim: the old rules (`PairMarkClear`, `ContactKills`, `HeldByPair`, the yield). Review
round (PR #85): "killed" is the engine's counted kill (`KillMoveEnd` 38614 kills too, read), a released hold drops
it, a refusal is skipped on watching games, a player joining mid-lend gets the NPC, summons are refused. Then the lend and a ride became one claim:
`HostClaims` / `ClaimService` hold any host actor for another game and take it back (`ActorGiveBackRequest`);
then a ride and a kill move became one action claim (`ClaimRequest`: its actors taken or
locked, all or none, one claim per actor); `HostPairs`, `PairService` and the pair messages are gone.

2026-11-19: **a mount handed back between pairs, piece 4a of one-simulator.md, built** (`agent/piece-4`): the horse
stuck in its rider's exit pair after a hand-back (PR #80 sent it `PairEnd` by hand) came from the rider's last graph ops,
its horse's `PairEnd` among them, being dropped unsent when it gave the horse up. Now the rider's game hands the horse
back once its next graph update is recorded, its ops sent first, and the host's game waits until its copy has run them
(`docs/re/systems/mounts.md`, "A horse handed back between pairs"). On the duo the host's next get-on is accepted at once
(five tries before). A passenger who drives on still takes the horse at once and ends the old driver's pair by hand: taken
after the replayed exit, it would not steer (not read yet). Kill moves (4b) next.

2026-11-19: **items as their owner's part, piece 3 of one-simulator.md, built** (`agent/piece-3-items`): an actor's items
and worn set are one record part (`kItems`, `ItemsPart`), declared by its owner when an item or equip hook, a 3D load or
the engine's outfit reset (TESNPC 418622, hooked) touches it, and carrying only the stacks that changed (merged onto the
version before; a gap asks the owner). A copy applies it once its 3D is in and again on each 3D load, outfit reset and
move to its owner's cell: counts by stack (`SetCopyItemCount`), then only the worn pieces that differ, each hand holding
its weapon, torch or spell (the full reset when one is refused). Nobody relays item changes any more: a host script's
change of a guest's character goes to that guest, a guest's loot is the host's transfer with the NPC's part after it.
Gone: the equipment messages and their 3 s snapshot, the spawn's inventory (`WaitingFor3D`, `InventoryContent`). Read in
IDA: 16059's removals, 24725/24384/418622/16100 on a 3D load, the queued-equip drain (inventory-changes.md, "1.7.104").
Closes "A looted corpse keeps items on the guest": the spawn put the bandit's base leveled lists on its copy as items
(measured, `inventory_entry`); `looted_corpse_empty_on_both` (MCP) fails 3 of 3 on dev, passes 4 of 4. Tests: GameTests
`[items-part]` (5), ClientTests `[items-part]`, `[equipment-repair]`, HostAuthorityTests (4). Open with leads: "A copy
without its weapon" (418622 empties a copy's hands, measured), "A spawn's inventory over a newer equip" (no MCP
scenario), "The guest player's copy wears nothing" (trigger unread), new "A party item removed without its tag". A guest's
loot no longer passes through the host's copy of the looter, and a stack carries its owner (`Inventory::Entry::ExtraOwner`):
16059's target only moved the item and stamped a theft's owner on it, so a guest's container theft was never stolen on its
own game (read in IDA; `guest-container-theft-is-stolen`, MCP with a new `expect stolen`, fails 3 of 3 on dev, passes 5
of 5; GameTests "A guest's theft from an owned chest is the owner's on the guest's game"). Also read: why the outfit
reset keeps a copy's items (our forced equips lock them, `ExtraCannotWear`; inventory-changes.md). Review of PR #83:
a put still moves from the host's copy of the player (spent at once against a second put), a player's copy keeps its
party stacks and wears the ones its owner does (the part carries them, the party's store counts them), a copy applies
its owner's shout, a record part can pass 64 KB, a version 3 character co-save still loads (version 4: owners).

2026-11-18: **actor state as record parts, piece 2 of one-simulator.md, built** (`agent/piece-2-actor-state`): a copy
gets what its owner set beside the graph from three new parts. `kActorState` (sneak, block, bleedout and the other
non-death life states, a kill move for a copy made mid-move, drawn hands) is read by the owner after each frame and
applied on a copy when its stream reaches that frame (`ActorStateService`, masks in `ActorStateRules.h`). `kFactions` and
`kName` replace `Request/NotifyFactionsChanges`, the spawn's factions and name, and the host's name relay; a script's
`SetDisplayName` or `SetName` now reaches the copies and the party's lists (no event: the owner's frame read; the
lists follow the player character's name part, `PlayerNameRequest` gone). An action on a copy's stream
gives its owner's flags but the part's bits, as before for every bit not read yet. Gone: `DrawWeaponRequest`/
`NotifyDrawWeapon` and the 0.25 s hands poll, `CacheComponent`. The design's `ExtraForcedTarget` for a copy's swing was
dropped: it skips reach (38608); the copy's `+0x104` already limits its swing to its owner's target (read in IDA, measured on the duo; closes "An NPC copy's swing picks the nearest")
([actor-state.md](re/systems/actor-state.md)). Tests: GameTests `[actor-state]` (9), two name flows, ClientTests `[actor-state]` (5), HostAuthorityTests;
scenario `script-renames-reach-party` (fails on dev); closes "A mod's rename at run time".

2026-11-18: the duo's ~400 ms stalls about once a second at a repro scene's start were the test harness's own
(`agent/duo-stall-profile`, [harness-cost.md](re/systems/harness-cost.md)). The command server built and wrote every
reply's JSON on the game's main thread, and the scene's world baseline asks for the whole town's `world_state` back
to back (in Riverwood up to 451 ms a call). `world_state` now copies the references out on the main thread and
builds its JSON on a worker (`Command::Gather`; replies still go out in order, `ReplyQueue`, ClientTests), and so does
the status file; other replies stay in their frame (a `console_result` held a frame got lost behind a load): `world_state`
went to 21 ms on the main thread, and the scene's settle window has no stall left. `profile` lists each command's
main-thread time (`harness <cmd> us`) and builds Tracy's Linux tools from any worktree. Players' builds are unchanged
(test builds only). `horse-passes-between-riders` still fails its last ride (the guest does not see the horse ridden),
before and after: that is the horse bug, not this.

2026-11-18: **a guest's rejoin works again** (`agent/rejoin-guest`): a join while joined made the guest's one-peer ENet
host store a null peer over its live one, so the host kept the old connection (and the guest's character) and every
later join was unreachable. `Net::Client::Connect`/`ConnectByIp` now refuse while a peer or a resolve is held, the
harness's join while joined fails at once, `rejoin_client` leaves first, and the MCP has a verb
`guest_leaves_to_main_menu` (join-flow.md, Leaving and kick). With rejoins working, "A copy that looks at nobody" is
closed: Lucan's copy looks at the host's player like its owner after both rejoin paths. Tests: HostAuthorityTests "a
guest's second connect keeps its first connection", GameTests `[rejoin]`; scenario `guest-rejoins-while-joined` (its steps pass; its end
comparison fails on bugs.md's open "References differ").

2026-11-18: **the copy record, piece 1 of one-simulator.md, built** (`agent/copy-record`): every game keeps each actor's
last declared state in versioned parts by session id (`Services/CopyRecords`), the host keeps them and puts them on every
spawn (`HostRecords`), a part waits for its copy and for an actor it names, and a game can ask for one again. One part
moved onto it, AI state: `AiStateRequest`/`NotifyAiState` and the spawn's `AiState` are gone; a look or combat target
named before its copy exists is applied when it appears ("A copy that looks at nobody": closed by the rejoin entry above, whose duo scenario
shows the player path). The other caches move in later PRs.
Tests: GameTests `[copy-records]` (3), ClientTests `[copy-records]` (6), HostAuthorityTests `[records]` (4).

2026-11-18: a horse ridden by the host, then a guest, then the host again shows the host riding on the guest's game
(`agent/horse-riding-copy`, `docs/re/systems/mounts.md`, "A horse taken back mid-pair"). The host took the horse back
while it still replayed the guest's exit pair, whose `PairEnd` then never came. The host's get-on was refused and
retried (39669 calls `InitiateMountPackage` 37905 once per try), and the retries left the guest's copy of the host off
the horse. Now the host's game ends that pair when it takes a horse back (`OnNotifyDismount`). GameTests: the fake
loses a copy mount's stream `PairEnd` once the game takes the mount, and refuses a get-on of a horse in a pair once
(`PairEndIn`, `MountRetryOf`); "A horse passes from rider to rider" checks both. `horse-passes-between-riders` passes.

2026-11-18: one sync rule for every system, designed ([one-simulator.md](design/one-simulator.md),
`agent/one-sync-model`). An audit's map checked in the code: each open desync is a copy missing what its owner did
beside the graph stream (39004's list, read in IDA) or two games deciding one thing. Ten pieces in build order: a
versioned copy record, actor state and items as parts of it, pairs claimed through the host, the host running an
activation's world part and creating what guests create, scenery left out of the world check. 40 of bugs.md's 81
entries are placed under a piece as its acceptance list; eight engine questions are open.

2026-11-18: release cleanup. Full GPLv3 `LICENSE` (the upstream notice in `NOTICE`); a short README (development is
Linux only); machine paths from the environment or `~/.config/skyrimcoop/paths.env` (`scripts/local_paths.py`), and the
duo runs as this user unless `COOP_DUO_USER` is set; `playtests/`, raw report evidence and the imgui examples removed.

2026-11-18: "The picked line overlaps" (bugs.md, Dialogue) is closed as not seen on dev. On the duo (`agent/bugs-dialogue`)
the party talked to Lucan at MS13's stage 60 and each pick, the first topic and the claw topic's two-line entry, showed
its line once on both menus, the picker's and the other's (screenshots, not kept). `ShowPicked` was rewritten after the report (b3a43770, 2026-11-01); the SWF's copy
already sits over the centre row (`docs/re/systems/dialogue.md`, "Where the picked line is drawn"). Its scenario,
`dialogue-options-are-the-partys`, already covers the pick.

The same day "Leaving a crafting station" (bugs.md, Party scripts) is closed as not seen on dev: the guest's copy got up
with it from QASmoke's cooking spit after a plain exit and after Tab right after the craft box's Yes (the reported path;
Tab with the box still up does nothing, the box takes only the mouse). New scenario `guest-leaves-crafting-mid-craft`
(two mid-craft exits a run) passes 2 of 2; the copy's exit path is in `docs/re/systems/actor-movement.md`, "Getting up".
MCP: verb `select_makeable_crafting_entry`. `host-beast-form-stays-host` passed 2 of 2 (the werewolf entry stays open).

2026-11-18: a chest's lid agrees after the host disables it under an open menu (`bugs.md`, "A chest lid's pose",
gone). The disable drops the chest's 3D (19801); our live menu's auto-close, or the host's close row on a guest, then
flips its open state with no 3D, and the enable's new 3D (19800) came up part open in each game where that happened.
Vanilla never gets here (its menus pause the game). Every game now puts such a lid at its open state once it is
enabled and loaded again (`LidUnposedEvent`, `ObjectService`; `SetOpenState` 14287 with its snap, as a 3D load does,
`InitNonNPCAnimation` 19757; `docs/re/systems/guest-world-events.md`, "A lid after a disable"). Scenarios
`guest-chest-lid-after-disable`, `host-/guest-menus-close-when-target-goes`; GameTests "A chest the host disables
under ... open menu shows one lid once enabled" (the fake learned lids, a chest's 3D and Enable's queued load). The
harness reads a container's open state (`open_state`), `door_open` takes containers, and the MCP's `expect agree`
runs the world check too.

2026-11-17: the party shares a horse (`agent/mount-per-player`, `docs/re/systems/player-mount.md`). One player drives,
the others ride along: a player's Activate on a horse a party player drives boards it (our hook on
`Character::ActivateMount` 37864 raises `BoardEvent`; the horse's slot emptied, then the engine's own get-on), and the
crosshair offers Ride over it (`HookGetActivateText` 24716). The host keeps each horse's passengers in boarding order
(`HostCharacters::Character::Passengers`, `BoardRequest` / `NotifyBoard` / `UnboardRequest`). When the driver gets off
or leaves, the first passenger drives (`HostCharacters::HandOver`, a `NotifyMount` to it): its game takes the horse and
the reins (`PlayerTakeReins`: the horse's slot, `PlayerCharacter::OnMountStarted` 40725, `Actor::EvaluatePackage`
37401, and `PairEnd` for the old driver's exit pair, which only a pair's instance sends, 32797); a horse handed to a
passenger already off it goes straight back. A copy on a mount: linked as a driver's copy is and seated as its owner's
get-on ends; its bodies in its mount's collision group, as `SeatRider` (40358) sets a rider's (in its own group the
horse stepped 70 units away); its capsule off while it gets on; seated, no steps and the engine's position writes taken
(`REFRSyncController` 20318 keeps its reference on the horse; refused, it stayed 1,100 units behind); at its get-off off
the horse at once (`FinishDismount` 38696); a rider off a horse that stays ridden is told to its copies (`NotifyUnboard`), as a
ride can end with no exit clip. A refused get-on gives the driver back its slot. Any rider's Detach empties the horse's one slot, so the driver still on
holds it again. Passengers arrive on foot after a party travel. Scenario `party-shares-a-horse` checks boarding, both
seated, driving, the handover, every get-on and get-off for jumps on both games and each riding copy at its horse; it
passes its steps, its end check failing on the plains' critters (`bugs.md`). Tests: GameTests `[passengers]` (the
fake's `MountSlots`, `Reins`, `SyncedMounts`), HostAuthorityTests `[passengers]`, ClientTests `ReplayState` rules,
encoding round trip.

2026-11-17: a guest no longer freezes when the host's temporary NPCs go. The playtest's "coc whiterun crashed the
guest" was a freeze: the guest deleted the copies, and the animation file job (63997) spun on a deleted copy's lock.
A copy's movement idle load (kind 47, a turn-to-walk) named the copy's process lock and was never cancelled. Copies now
hold those sets with their own lock and callback, kept until none of their loads is queued (no cancel: 63997 takes a
finished load's lock before it reads a cancel)
([animation-file-loads.md](re/systems/animation-file-loads.md)). Tests: GameTests "A copy deleted while its movement
idle's clips load...", ClientTests `[held-clip-loads]`, scenario `copy_deleted_while_clips_load` (harness `clip_loads`
holds the guest's file job and lists each load's lock owner).

2026-11-17: a fight with one player is the party's (Solitude playtest, 2026-11-16: the platform guards killed Josh, a
guest, and never turned on the host). The execution's interrupt trigger runs `PlatformGuard.StartCombat(Game.GetPlayer())`,
the guest's copy for a guest's trigger: no crime, so nothing the party shares, and `HostAggro` counted only damage and
crime fights. Now every NPC whose combat target is a player is a party fight (`PartyThreat::State::PartyFights`, nearest
player first) and every player's body is its enemy in the reaction hooks (`PartyAggroNative::IsPartyEnemy`), so its
combat group can take them ([combat-targets.md](re/systems/combat-targets.md), A fight with one player is the party's).
A guest's friendly-hit anger is the party's as well (`PartyThreat::AngerCovers`; scenario `guest-angers-ally-party-enemy`;
MCP: `fight(until_dead=False)`, verb `expect_enemy`, a new duo forgets the last games' scene and staging).
Tests: GameTests "A guard a script set on the guest turns on the host beside it" (the fake's group takes only targets
its members can attack, `CombatFriendly`), ClientTests `[party-aggro]`, scenario `guest-trips-platform-guard` (its steps
pass; its end comparison finds the town's scattering crowd apart, bugs.md "End comparisons in towns").

2026-11-16: save sync. A guest waited 5-10 s at a host load where the host waited none: the save (3 MB) travelling,
not its load. A save now travels as its difference from one the guest holds (its body decompressed first: the
engine's LZ4 hid what two saves share), and every save the host makes goes ahead to the guests as it is written, so
a load of it sends only its id and both games start loading at once ([join-flow.md](design/join-flow.md#save-sync-2026-11-16)).
On that session's saves: 3.05 MB before, 0.16-1.07 MB against a held save, 1.55 MB whole (a first join). The block
reuse of 2026-11-03 is removed (3-23% of blocks matched). Engine facts read in IDA
([save-game.md](re/systems/save-game.md), "The file's compression"): a type 0 save with a raw body loads (109334), and
a quicksave's file is written by the save thread after `Save_Impl` returned (109353 hooked, `SaveWrittenEvent`).
ClientTests `[save-transfer]`, HostAuthorityTests `[save]`, GameTests `[save-sync]` (a quicksave held then loaded with
nothing sent, a save never sent, a store that lost one, a newer save replacing one on its way, a rejoin); the fake
engine learned the save thread's late write. In Skyrim (QASmoke, one machine): `guest-reuses-world-cache`
passes (a first join 1.41 MB for a 7.8 MB image, the next 0.18 MB against the held one: the engine loads the type 0
file); `host-quickloads-party` and `death-wipe-checkpoint` pass; in all four load scenarios the guest held the save
within a second of its writing and the load sent 0 bytes (the guest back in 0.9-1.3 s, the reload's 200 ms update
stall on the host gone). `host-loads-party` and `host-menu-loads-party` then fail on the open bow bug
([bugs.md](bugs.md), "A fresh copy loses what its owner equipped right after spawning"), as on dev. Not measured: a
real line (the playtest's 5-10 s); a save ahead costs the host's upload 0.05-1 MB a save.

2026-11-16: a party wipe no longer loads a save by itself: everyone stays down, the screen tells the host to load a
save and the guests that the host chooses, and the dead host loads one from the pause menu or quickloads (a host load,
`host-loads.md`). The engine allows it: `MenuOpenHandler::ProcessButton` (52249) opens the Journal while the player is
dead and `CanLoadGame` (35736) checks no life state (read in IDA; death.md, "Wipe"). Removed the wipe's automatic
reload (`HostGame::PartyWipe`, `PreviousSave`, `PreviousGameplaySave`) and the session freeze while wiped (chat and
joins go on). GameTests "A party wipe loads nothing until the host loads the save it chooses"; scenario
`death-wipe-host-loads` (replaces `death-wipe-checkpoint` and `death-wipe-twice-keeps-character`).

2026-11-22: `/killmove 100` makes every swing a kill move. The odds kept the engine's fatal test, so at 100 a kill
move still came only on the killing blow, which the game's own rule mostly gives already: the rate looked unchanged.
Now with odds set `ShouldAttackKill` answers `Actor::WouldAttackKill`'s (38559) checks before its hit probe (a living
victim the attacker may kill, 37229) and the roll ([dev-commands.md](re/systems/dev-commands.md)). ClientTests
`[dev-commands]`; scenario `killmove-odds-every-swing` (one swing at a 1000-health wolf, guest and host).

2026-11-16: dev chat commands for playtests (`/dev` lists them): `/heal`, `/god`, `/speed`, `/gravity`, `/shouts`,
`/gear`, `/lootbox`, `/killmove`, `/reset` on every game's own player; `/spawn`, `/killall`, `/weather` in the host's
game at the typer's character. Anyone types them; the host runs world commands and sends the line to every game, which
runs console lines on its own player ([dev-commands.md](design/dev-commands.md)). Kill moves have no chance in vanilla
(the root idles need a fatal swing on the last enemy, 21744/21743): `/killmove` hooks those two conditions
([dev-commands.md](re/systems/dev-commands.md)). Tests: ClientTests `[dev-commands]`, HostAuthorityTests `[DevTools]`,
GameTests `[dev-commands]`; checked by hand in Skyrim.

2026-11-16: copies hold their owners' furniture props. Alvor hammered at the forge with no hammer on the guest: a prop
is an animation object (ANIO) the idle's first clip loads and draws (AnimObjLoad/AnimObjDraw, 42888/42889), and a copy
starting from a later sync point (its 3D reloaded, a guest arriving) skipped them. The sync point now carries the
owner's props and the copy holds them as a save's load does (43580: Load 43571, Draw 43572)
([animation-objects.md](re/systems/animation-objects.md)). Tests: GameTests `[props]`, ClientTests `[graph-stream]`,
scenario `copy_holds_furniture_prop`; snapshot readout `state.anim_objects`, MCP `expect(check="props")`.

2026-11-16: a wheel let go beside a pile keeps falling (the playtest's cheese wheel stopped in the air). Our island
sleeps (`hkpEntity::deactivate` 60850) wrote HK_REAL_MAX into each body's fastest speed, which passes Havok's last
sleep test for a body that moves, and the host's move row of a guest's own pile rest came back and put one to sleep
beside the falling wheel ([physics.md](re/systems/physics.md), "When an island sleeps"). Our sleeps now keep those
speeds and the guest skips its own rest's row ([physics-sync.md](design/physics-sync.md), "A rest's move row comes
back"). Scenario `guest-pile-release-falls`; GameTests "A guest's player knocks the bucket and it rests: the host's move
row of that rest does not put it back"; trials `stack_release` and `pile_carry_release` in the vanilla baseline.

2026-11-13: a guest's seen theft is warned, as the host's is ([crime.md](re/systems/crime.md), "The steal warning").
Read in IDA: the warning's package (37556) and its step (procedure 40, 39673) name `g_thePlayer` throughout, so it
walked to, faced, warned and took back from the host's player; it is now started for the guest's copy (seven call
sites swapped in `CrimeBridge`) and the given-back runs on the guest's game (`NotifyCrimeConfiscation` gains owner and
destination), where the item's ownership is. Also found: our `Confiscate` hook (16120) cut its owner-filter pointer to
32 bits, so a warning never took anything back, the host's own included. Measured: an NPC in a scene (Orgnar's bar
scene) drops the warning before its first step, for both players, and the theft is reported. Scenario
`guest-theft-warned` (MCP: `stop_scene`, `expect_given_back`, `expect_warnings` waits); GameTests "A guest's seen theft
is warned", ClientTests `[CrimeRules]`, encoding round trip. `guest-theft-at-bar-bounty` now spends the warnings first.

2026-11-16: dialogue polish, the mouse and joins. The mouse moved everyone's option: on PC the dialogue menu has a cursor
and a hover selects (vanilla, `DialogueMenu` flags read in IDA, 82310), and every highlight change was sent to the party.
Worse, a still cursor undid other players' moves: the party's highlight recentres a menu without the list's animation,
the row sliding under the resting cursor rolls over and was sent back (`dialogue-resting-cursor-keeps-party-highlight`,
2 of 2). Now a hover is its own menu's (a change within two frames of `MenuCursor` moving), W, S and the wheel move the
party's (`DialogueMenuPolicy::OnHighlightMoved`), and the bridge holds the rows' rollover (`bListAnimating`) from its
recentring until the cursor moves; checked on the duo.
A guest joining mid-line heard the NPC's line twice (`ReplayGreeting` on a join; `SpeakSound` 37542 stops nothing): the
replay is now only for a menu its own ask opened. Checked and fine on the duo: a join keeping members' highlight, the host
joining a guest's conversation, a join in the frame of a pick. A guest asking an NPC again
right after a conversation closed lost the greeting's choices (8 of 8; one player alone never): the host's hidden start
checks "anything left to say?" with `FirstOption` (35267), a cursor that left the engine's picked option on option 0, and
the build after the NPC's last line listed that option's choices; `MenuTopicManager::HasOptions` now leaves it as it
was (6 of 6 kept; [dialogue.md](re/systems/dialogue.md), "A start during the NPC's last line"). GameTests
"A player's mouse moves only its own menu's highlight...", "A list sliding under a still cursor...", "A hover kept
through a list refresh...", "A player joining while the NPC answers hears its line once", "A guest's ask run without the
host's menu keeps the greeting's choices..." (each fails with its fix taken out); ClientTests `OnHighlightMoved`,
`OnFirstHighlight`, `RolloverHold`, `AskKeepingPicked`; scenarios `dialogue-resting-cursor-keeps-party-highlight`,
`dialogue-guest-joins-mid-line`, `dialogue-guest-reasks-after-close` (MCP: `guest_replays_mark`, `expect_no_line_replayed`,
`talk_again_as_menu_closes`, `reask_after_close`, `expect_reask_keeps_choices`; harness `dialogue`: `picked`, `last_selected`,
`last_speaker`).

2026-11-13: an NPC talking with a guest looked at the host's player, on every game. The host's game runs a guest's
conversation with its own player, and the engine puts that player in the NPC's dialogue headtrack slot at the start and
with each line (`ActorProcess::SetDialogueHeadtrackSlot` 39465 from 37200 and 37541, read in IDA); the copy takes the
owner's slots. Now the host's game names the player talking (the guest who started it, then each picker) where the
engine writes the slot (`HookSetDialogueHeadtrack`, `HostConversation::LookAt`; `NotifyDialogueStart` carries the asker),
so every game's NPC looks at that player ([dialogue.md](re/systems/dialogue.md), "Who the speaker looks at"). When the
player it faces leaves while others stay, it turns to the first player still in it (`NotifyDialogueFace`, from the host's
rules). Its body turns to that player too: the engine turns the body only to its own player in a conversation
(`Character::ShouldRotateToTrack` 38011), now also to the named talker (`HookShouldRotateToTrack`). GameTests `[headtrack]`
(each fails without it), ClientTests, HostAuthorityTests, the scenarios `npc-looks-at-guest-talker`,
`npc-turns-when-talker-leaves` and `npc-turns-body-to-guest-talker`.

2026-11-13: a guest's held E on a bottle on Orgnar's bar grabs it again. Skyrim wakes resting clutter it never
moves: Orgnar's animated body shares the bar bottles' island and re-activates it on each animation update (78910 ->
`hkpEntity::Activate`), and a character's proxy activates what it touches each step (62648); found with the new harness
probe `activate_watch` (MCP verb `who_wakes`), the same with our physics hooks left out. The host's `PhysicsBodies`
took "island active" for "moving", so each wake keyframed the guest's bottle and the engine's grab
(`CreateMouseSprings` 40555) refused it: the release picked it up, a theft. Now a body sends frames only once it has
left where it woke (`physics.md`, "Woken still: awake is not moving"); its rest still goes. A guest's seen theft is no
longer warned: the steal warning (37556) goes only to the host's player, so the guest saw neither warning nor bounty
(`crime.md`). Scenarios `guest-grabs-orgnar-bottle`, `guest-theft-at-bar-bounty`; MCP: `who_wakes`, `vanilla_start`'s
`disable_hooks`, `set_warnings`, `expect_warnings`; `grab` takes handle names.

2026-11-13: `coop_test.py play` follows: the game you don't control walks after your player and faces it, so the watch
view shows your player from the other game ([testing.md](design/testing.md), "To play by hand"). Both ways (`--control host`
or `guest`). Its player runs AI-driven on `KeepOffsetFromActor`, a `PathToReference` leg when stuck, checked placement
only when left far behind or stuck twice; chat `!stay` / `!follow`, `--no-follow`. Pure part in
`scripts/coop_harness/follow.py`, units `test_coop_harness.PlayFollow`. KeepOffset's engine side:
[actor-movement.md](re/systems/actor-movement.md), "KeepOffsetFromActor".

2026-11-09: both players at once (`dialogue-stress`). The MCP's `together` runs lanes (any tool or verb) from one
moment; a lane's first key or mouse input is queued in its game (`TestHarness/Scheduled`, any command's `at`) and
lands in one frame on both games (0-14 ms apart measured). `line_delay(ms)` puts netem on the guest's namespace link,
so races two machines meet show on one. First bug from it: two players moving the dialogue highlight at once ended
each menu on the other's move (host 2, guest 0; `dialogue-highlights-cross`, GameTests "Highlights moved on two games
at once..."). The host now relays each move to every menu, the mover's too, with the mover's count (`Seq`): every
menu ends on the order moves reached the host. A pick that seemed stuck mid-greeting was the harness: `stage`'s
default freeze clears the speaker's `kProcessMe`, its line never ends and `SaySelected` (35283) waits for it;
`talk` refuses a frozen speaker now ([dialogue.md](re/systems/dialogue.md), "Two players at once"). The host joining
while the NPC answered a guest's pick saw the last step's list and moved its highlight alone
(`dialogue-host-joins-mid-line`, GameTests "A player joining while the NPC answers..."): the pick now carries its
text and a joining menu shows it. New scenarios that pass: `dialogue-picks-cross-frame` (both press E in one frame
over 40 ms: one line said; a press can say an option another player highlighted milliseconds before, by design),
`dialogue-hover-while-guest-picks`, `dialogue-two-starts-at-once` (one conversation, the other ask forgotten). `dialogue-guest-walks-every-option`: the guest says each
of Andurs's first answers (Tab and talk again between), the host joining each conversation and never picking; one
line per pick, the menus agree. Following a path into a goodbye and talking again at once finds the NPC with nothing
to say (37200 returns true when StartDialogue opened nothing; answered since dev's `cb60efb2`), so the walk stops
short of goodbyes. A joining menu keeping its own highlight (code review) did not show in Skyrim: the host joining
after a listed topic was said came up on the party's highlight (`dialogue-joiner-keeps-party-highlight`); the bugs.md
entry is gone, the scenario and GameTests flow guard it.

2026-11-10: the graph input stream's phase 4, players' copies. Each game records its own player (its third-person
graph) and the host passes a guest's ops and sync points on by owner. Fixed on the way: a kill move replayed whole
(an owner stays off its stream to the end of its pair's clip), sneaking never shown (an action carries its owner's
state after it), a rider's copy stuck getting on, then stuck riding (a copy gets off by its clip's own exit event:
it stays off its stream until its sit state leaves wants-to-stand). `teleport-keeps-copies` failed on a stale
reading, not a graph: a player's copy is culled like any actor, and the observer now skips its pose while it is not
drawn (`docs/re/systems/culled-npc-pose-readings.md`). The guest crash is explained and fixed: a sync point taken
while a walk's direction blend cross-blended put a copy's generator in that mode with no blenders; an owner now takes
no point until the cross-blend ends. `guest-rides-host-horse`'s head-tracking failure was not the stream's: the
engine drops a look at this game's player its tracker does not detect, and a player's copy detects nobody; a copy's
tracking no longer asks. A rider's copy in "deferred kill" after the ride is the copies' own, by design (`docs/design/graph-input-stream.md`).

2026-11-10: the graph input stream's phase 3. A streamed owner's movement carries no graph variables and its copy's
graph loads none; graph points and every-N counters (`GraphRandom`) are removed, with the branch's interim fixes
(refused actions' variables, actions spaced on the copy's graph time): a copy off a stream plays actions at playback
again, as on dev. On the duo the counter scenarios pass on the stream alone, the kill-move scenarios pass with dev's
timing (the guest's blow 65-104 ms after the host's, the copy dead 111-135 ms after its owner), and the wolf's and
Whiterun's live checks hold. `copy-strafes-with-host` is not the branch's: dev fails it as often (12 runs in 12 back to
back, the branch 9), on long frames and on the copy's capsule catching at one spot (bugs.md, A strafing copy catches
for a frame). One more `kill-moves-cross` shape filed, dev's by its code (Crossed kill moves: a short winner's blow
lands first). Left: players' copies (phase 4).

2026-11-09: other players and moving objects are smoother (`copy-clock.md`, "Measuring smoothness"). The MCP's new
`smoothness` tool measures a copy's speed per engine frame against its owner's. Fixed from its baseline: positions
went in whole units (a walking copy's frames over 20 % off: 35 % to 7 %, its owner's 7 %); physics frames were stamped
and played on the uneven session time (S10 for bodies, `physics-sync.md`); a late snapshot after the short guess
jumped the copy back (now blended over 100 ms); a standing snapshot is 50 bytes (was 112). Read in IDA: where the
Havok step and our update fall in the frame (`actor-movement.md`). With the graph input stream (2026-11-13): a moving
player costs about 3.5 KB/s on the wire, a 15-wolf fight 32.7 KB/s to each guest (was 76).

2026-11-09: a host NPC's copy no longer sheathes mid-fight: its knowledge of its owner's hands was reset from its
spawn when its 3D loaded, over a newer draw (`animation-replay-recovery.md`, "Why copies refuse", 4). The same
measurement found why copies refuse actions (their own animation graph channels, timing, backlogs); those fixes were
dropped for the graph input stream (`copy-idle-graph`).

2026-11-09: the two kill-move leftovers of the stream's combined run, fixed. `kill-moves-cross` (2 fails in 5, none
on dev): the host's pair waited 330 ms on the guest behind a `moveStart` its copy's graph clock held back, and was
refused once the guest's player had died; a pair or a death now comes at playback with the actions before it
(`AtPlayback`; 8 passes in 8). `host-kill-move` (the guest's blow 20-70 ms before the host's): a replayed pair with a
copy partner makes no instance on the guest, so the wolf's copy took the kill move from its stream, where its
synchronized clip skips its owner's 150 ms wait at the mark. An owner in a pair is not recorded now, and its copy
leaves the stream at its half's action until a sync point from after the pair, as before the stream: the guest's blow
35-123 ms after the host's (dev 62-65), the copy dead 100-144 ms after its owner (dev 95-150), 4 runs. Three GameTests
flows; both bugs.md entries gone.

2026-11-09: the stream's combined run (21 scenarios tagged animation or copies) and what it found. A copy off its
stream died up to 250 ms late: its death was spaced from the last action it replayed before the stream
(`host-kill-move` 2 fails in 4; now none in 6, GameTests "A copy back off its stream dies..."). A copy whose point was
another project's never asked again with its new graph (`GraphStreams::SeeGraph`, found by its new GameTests flow).
The fake no longer chunks a recorded far owner or runs a generate pass in a frame without a step. Left: the guest's
kill move still a little ahead and `kill-moves-cross` failing its end comparison 2 runs in 5, 0 in 6 on dev (bugs.md,
A host's kill move runs ahead on the guest; Crossing kill moves leave a copy's last action wrong); `copy-strafes-with-host`
stutters on dev's plugin too (2 runs in 3); `placed-speaker-deleted-mid-conversation` fails on dev since `talk`
refuses a frozen speaker (its stage freezes Vilkas).

2026-11-09: the graph input stream's owners step every frame without `force` (creatures no longer hover), a graph's
generate passes are in the stream, sync points carry nodes' plain members and the active-node flag, and a copy in a
pair on its game leaves its stream for it: a guest's kill move on a host NPC works again (all six kill-move scenarios
pass). The hover was `force` turning foot IK on for unseen actors (graph-input-stream.md, Owner); copies replay
without it too. A copy knocked down is held off its stream until it stands (it never got up: its owner's get-up
action was bookkeeping). Open: the fake still chunking recorded owners; the get-up's snaps and jitter (bugs.md).

2026-11-09: copies on the graph input stream match their owners at every update on the duo: the wolf scenario's 180 s
and Whiterun's market for 120 s, every streamed copy ([graph-input-stream.md](design/graph-input-stream.md), A graph's
whole state). A sync point is now the owner's whole behaviour graph through Havok's own save and restore
(`getInternalState` 58372 / `setInternalState` 58373): each node's state, a running transition's effect inside its
machine's, BS nodes' runtime members by reflection. Read in IDA and measured: Skyrim's `updateActiveNodes` walks with
59319, which drops the restore map, so the restore hands that walk to Havok's own 59318 while it runs; its activations'
events are dropped (the owner's queue is empty). The owner's clip loads (action, instant interaction, movement idle,
a save's reload) are ops in the stream, and a point's sets load before it restores. The live check now says why a copy
parts: variables, handled events with their sender, clip times. GameTests: a copy starting mid-blend, after an idle,
through a turn its owner loaded nothing for. Both scenarios still fail their end comparisons, on ambient differences
filed under bugs.md's End comparisons in towns (Whiterun's containers; a creature's copy 57 units off in height).

2026-11-09: a guest holds the NPCs its own engine runs until the host spawns them (copy-contract.md, "A guest's own
NPCs"). Aela, Ria and Farkas stood in Whiterun's market on the guest, running their own AI, while the host had them in
Jorrvaskr. Each game's engine walks a non-copy NPC's schedule on its own clock (`UpdateAI` at every process level).
Decided: the host ignores them, and the guest gives them no AI (`HookUpdateAI`) and no 3D (`HookLoad3D` on 19727, the
disabled path), until the host's spawn makes them copies and queues their 3D (`QueueReference` 13057). A horse the
guest rides is its own. Tests: GameTests `[guest-own-npc]`, and an actor checksum that fails any guest showing an actor
of its own; ClientTests `[GuestNpcs]`; scenario `guest_shows_only_hosts_npcs` with the MCP's new `expect guest_npcs`
(`actor_place` also reports the package and process level). On the duo its steps pass; its end comparison in Whiterun
fails on the town differences in bugs.md ("End comparisons in towns", "Doors the host's NPCs unlock").
From the PR's review: NPCs already shown when holding starts (the host's save loads before the guest connects; a
rejoin) are reloaded so their 3D is refused (32 hidden at a Whiterun rejoin, each a copy again within 10 s), and a
refused load job ends in `QueuedReference::Finish` (13022, hooked), where the NPC's 3D is queued again if a spawn came
while the job was out, and where a model loaded before holding started is reloaded as it attaches. GameTests flows for both; the scenario now leaves and rejoins.

2026-11-08: two of the playtest's dialogue bugs fixed. A seated copy takes the crosshair again: the engine picks a seated
actor only through its own bodies, which its AI puts keyframed on layer 33 in the world as it sits (40063); copies run no
AI, so theirs never were. A copy's bodies and graphs now mirror its owner's, as its capsule does: each movement snapshot
carries the layer the owner's bodies are on in the world and whether its graphs step with the physics world (40063
detaches them, or the ragdoll driver would drive the bodies) (`Movement::BodiesLayer`, `GraphsStepPhysics`). And a guest's ask always gets an answer: `SetDialogueWithPlayer` (37200) returns true even
when `StartDialogue` (35282) opened nothing (an NPC with nothing to say: Hilde, most guards), so the host's game now checks
its speaker after the start and the guest reads "They have nothing to say."; an ask to talk to a player's character is
refused. One conversation for the party stays. `re/systems/dialogue.md`; GameTests "A guest's crosshair takes an NPC copy
seated in its furniture", "A guest talks to an NPC with nothing to say"; HostAuthorityTests; scenarios
`guest-crosshair-skips-leaning-npc`, `guest-talk-nothing-to-say`.

2026-11-08: hardening, the other player's copy can't hold the stealth eye open ([stealth.md](re/systems/stealth.md));
the playtest's report stays open in bugs.md (not reproduced). Read in IDA:
`HookUpdateDetectionState` (42704) skipped the copy's pass, which left its request queued for good, and 42702 answers a
queued knowledge's state at any age, so a level installed before stayed. Measured on the duo the copy held none
(standing and sneaking, both games). The hook now installs an unseen pass (`DetectionRules`), and a player's copy is
flagged `kDoNotShowOnStealthMeter` (41408 skips it, as MQ201 does for its allies), which also covers 37765's forced
detection (42705, no pass). The playtest's moment was not reproduced. Tests: `detection_rules_tests.cpp`, GameTests
`[stealth]`, scenario `other-player-looking-leaves-eye-hidden`; harness `crime stealth_meter` names each source.

2026-11-08: a party wipe right after a wipe reloads the host's own character. The engine takes a save's character id
from its file name, so the scratch `SkyrimCoopWipe` load left id 0 and the next wipe took the newest id-0 save
(another character's); the host now gives the engine the checkpoint's character back after the load
(`SetCurrentCharacter` 35751; save-game.md, "The character a save belongs to"). GameTests: two wipes in a row, a host
load of another character's save; duo scenario `death-wipe-twice-keeps-character` (fails at wipe 1 without the fix:
the host's id left 0). The guest's crash in that playtest's load is a stale form, not the save (bugs.md).

2026-11-08: a guest's caught pickpocket reaches the host (playtest: "nothing happens"). The game raises it with no item
and count 0 (`AttemptPickpocket` 40654 -> 37423; `crime.md`, Pickpocket), and the guest's hook, the request's check
and the host's judgment each wanted an item, so it was dropped on the guest. It now names only its victim and the host
raises the alarm on its NPC. A take was already a container transfer, applied once in both games on the duo; the
playtest's "multiple copies" did not show. Scenario `guest-pickpockets-lucan` (the MCP's new `pickpocket` verb and
`expect` `bounty`), GameTests `[pickpocket]`.

2026-11-08: from the playtest with the user's brother, a guest's crosshair never takes an NPC copy leaning on a counter
(Lucan, sit state 3; the playtest's Orgnar): no name, no Talk prompt. Standing, it is taken; the host's crosshair takes
him either way (measured on the duo). The engine's pick is read (`PickCrosshairReference` 40620,
`CrosshairPickData::Pick`, `CanPickRef`): its furniture check passes on the copy, which has no furniture while leaning
(the host's Lucan has his counter). The cause in `Pick` is still to be read. Scenario
`guest-crosshair-skips-leaning-npc` (fails); the MCP's new `expect` `crosshair`.

2026-11-10: a party crime reaches every player ([crime.md](re/systems/crime.md), "Who an NPC fights for a party
crime", "Every player hears the bounty"). Read in IDA: a theft's witness is only angry (+0x204 bit 0x800) and a guard
answering the alarm only on the crime's witness list (KnownCrime 41354), and `CriminalInterest` checked neither, so
their fights kept the host as target; now they are crime fights and go for the nearest player. The "N bounty added to
<hold>" line (37679) showed on the host only; the host now relays each report (`NotifyCrimeState.Reports`) and guests
show the engine's line. Scenario `guest-theft-bounty-message` (failed before), flows "A theft's witness, only angry",
"A guard answering a party crime", "A guest's witnessed theft shows the bounty line in every game", HostAuthorityTests
and ClientTests `[CrimeRules]`. MCP: `expect_bounty_message`.

2026-11-08: copies move toward the graph input stream ([graph-input-stream.md](design/graph-input-stream.md)): a copy's
graph fed its owner's inputs matched it on every update over the network (Faendal 17009/17009). The ops' encoding
(`Structs/GraphOps.h`: only variables set from outside the graph, one bit for an empty update) weighs 559 B/s for a
humanoid doing chores, about 10 for an idle wolf. The stream is built for host NPCs' copies in production code
(`GraphStream.h`, `GraphOpsRequest` / `GraphSyncRequest` / `GraphSyncPointRequest`) and runs in GameTests on the fake
engine's new behaviour graph (`FakeGraph.h`): every wolf flow passes a live check of the copy's graph against its
owner's at every point, which they all failed without it. In Skyrim (hooks on 63587 and 63591), the duo's live check
(`graph_stream_check`) found two production bugs, fixed (clip files, a late spawn state), and copies apart soon after
their sync point. Sync points now carry every active node's memory (clip times, modifiers' state; generic, by Havok's
class reflection): 23 copies in Riverwood matched their owners on all 6608 updates of a 120 s live check. An engine bug fixed on every game: range
modifier clones kept uninitialized bounds (`GraphCloneFixes.cpp`).

2026-11-08: a refused action's graph writes reach its copies. A machine's start can be a graph variable (the wolf's
`StandingBehavior.startStateId` <- `iSyncTurnState`, written into the new clone by its binding set, 59467), read when it
activates at a clip's end with no action; so is a transition's condition (CannedTurnState's exit on
`iSyncIdleLocomotion`). Their writer is an action the owner's graph refused: PerformAction (38949) still runs its
variable writes (39004; a refused `TurnStop` sets `iSyncTurnState` 1), and we sent no refused action, so the copy read
the old value: it turned on the spot for 4 to 20 s or stayed in its canned turn. A refused action that changed its
owner's variables is now sent as variables only, loaded in step on the copy with no event (`ReplayStep.h`)
([animation-replay-recovery.md](re/systems/animation-replay-recovery.md), "A refused action still writes its graph variables"; reading integers ahead
and reconciling machines were tried and parked there). Harness: the animation trace records each event sent into a graph
with its engine callers (`input`); `graph_machines` / `graph_machines_watch` read state machines on both games;
`name_owner`; `expect` check `graph` (its `value`: the owner changes a run needs); behavior_dump extracts creature
behaviours.

2026-11-07: the shared duo can't be held for long while others wait (testing.md, The duo lock). An interactive hold
(the MCP's `duo_start`, a keeper's) goes to the first in line after 15 minutes held with someone waiting; that waiter
stops its games and takes it, and the old holder queues again. A hook (`scripts/duo_hook.py`) reminds a session holding
the duo while others wait to free it when it reads code, uses IDA or builds; the watcher messages an idle holder's
worker (`scripts/duo_nudge.py`, 2026-11-08). Tests: `DuoLockTests`, `DuoHookTests`, `DuoNudgeTests`.

2026-11-07: a guest's theft of an item the host's game made is judged ([crime.md](re/systems/crime.md), "A created
item's theft names its owner"). Read in IDA: the player's pickup (40533) gives `StealAlarm` a created item's owner
actor, not the item, and the host's permission recheck (`IsAnOwner` 20210, which reads any NPC as the player's) stopped
it silently. A theft now carries the item's owner (`CrimeRequest::Owner`); `Judge` skips that recheck and its same-cell
check for an actor target (`CrimeRules`) and alarms with the owner. The bugs-map guess (created forms' session ids) was not the cause:
the item is not on the wire. Scenario `guest-steals-created-item`, flow "A guest steals an item the host's game made",
ClientTests `[CrimeRules]`.

2026-11-07: a copy's lifecycle is the host's events alone ([copy-contract.md](design/copy-contract.md), "A copy's
lifecycle"; engine path [copy-lifecycle.md](re/systems/copy-lifecycle.md)). A guest hides a held copy once, as the host's
removal reaches it and before it follows (`CopyHeldEvent`, `GuestCopies`): kept persistent for the session, no cell
unload frees it (19113); disabled, its 3D never loads (`Load3D` 19727); and its enables are refused (`HookEnable` 19800,
`HookSetDisabled` 14646, by form id in `HiddenCopies`). Gone: `GuestCopies`' per-frame scan and `RunPresence`'s sweep of collected references; the
placed guard is now kept, so the host's next spawn finds it. Olava's whereabouts and the deleted-speaker hook stay.
On the duo, `held-copy-goes-with-owner`, `leveled-placed-npc-is-hosts`, `npc-sent-home-copy-follows` (now expecting the
guest's Vilkas hidden), `npc-disabled-copy-follows`, `remove-host-npc`, `actors-walk-after-arrival` and
`placed-speaker-deleted-mid-conversation` pass twice in one session. The last marks the host's Vilkas deleted, so the
Vilkas scenarios run after it in the same session fail at their start (it ran last).

2026-11-07: the lock checker tells orders the frame keeps apart from a risk ([lock-order.md](re/systems/lock-order.md), "How the
checker tells them apart"). Each thread carries its stretch of the frame (main loop tasks, our update), each order every stretch
it was seen in; an inversion between main loop tasks and our update alone is logged as kept apart and fails no flow, anything
else still fails, and the live overlap check is as it was. `guest-rides-host-horse` failed on one after a respawn (bugs.md).
ClientTests `[locks]`, a harness unit.

2026-11-07: the stray empty box in fresh worktrees' test games is the main menu's Anniversary Edition box (52156),
queued while SkyrimPrefs.ini has no `bFreebiesSeen=1`: the harness's `coc` took it into QASmoke, empty (its body key
has no translation), and the duo boot then failed to host. `prepare_prefix` sets the pref; a fresh prefix boots,
hosts and joins with no box ([message-boxes.md](re/systems/message-boxes.md); harness test
`test_fresh_prefix_has_seen_the_anniversary_prompt`). Left open: why the game's main loop stops about 2 s after that
box opens outside a session (Loose ends, Tests and harness).

2026-11-07: the created chest checked in Skyrim: `created-chest-reaches-guest-empty` passes on 1.7.104 (3 of 3, and
played through the MCP: both games hold the garnet and 50 gold before and after the guest opens it), so the
2026-11-04 fix holds and nothing more was changed. Read in IDA why the guest's open adds nothing: the engine rolls a
container when it is made, not when it is opened ([guest-world-events.md](re/systems/guest-world-events.md),
"Created containers").

2026-11-07: copy state from Riverwood, rechecked. Hands: a copy's spell could stay selected with its hand empty
(after a trip that unloads its 3D before its spells are put back: `EquipSpell` holds only with a process), and the
reconcile compared only selected spells; it now also reads a loaded copy's hands
([copy-hand-spells.md](re/systems/copy-hand-spells.md), "Selected, not held"; scenario `copy-holds-spell-after-trip`,
its hand checks failing then passing on the duo, its end comparison failing on bugs.md's "End comparisons in towns";
GameTests and ClientTests `[hand-spell]`). Level: two placed Whiterun Guards read 1000 on
both games (the 2026-11-04 fix, default boot). The late-save cases (the Riverwood guard, Odahviing) are not rechecked:
booting the duo from `LateAfterDragonslayerBackinSkyrim` froze the host twice then (it boots in 36 s since, 2026-11-16).

2026-11-06: a copy with no owner goes ([copy-lifecycle.md](re/systems/copy-lifecycle.md)). A persistent NPC sent out of the
loaded cells says where it went (Olava); a guest hides a held reference with 3D in its area (`GuestCopies`), and drops one
its engine collected (the placed guard; `leveled-placed-npc-is-hosts` passes). A placed speaker marked deleted no longer
crashes either game (`HookUpdateInDialogue` 37541). Sigurd's registration stays open, not reproduced. On the merged code,
`held-copy-goes-with-owner`, `placed-speaker-deleted-mid-conversation` and `leveled-placed-npc-is-hosts` pass; the
combined batch is left to the next combined run. Filed: the MCP's crash check reporting a crash from before a duo restart.

2026-11-06: two checks that reported what did not happen are fixed.
  - **Drift:** the plugin's `Games/Animation/GraphDrift` is gone: it compared a copy with the action's `GraphState`, the owner
    mid-transition. `coop_test.py scenario` now reads each NPC's owner and copy live through each flow
    (`comparisons.GraphApart`, a `copies' graphs, owner against copy:` line, logged, never failing). "A copy walks while its
    owner turns" and "A copy's idle variant" deleted; "A copy's graph after a turn or a pick" is what the live check found.
  - **`ai_state.head_track`** compares what the engine reads (`Diagnostics/HeadTrackRead.h`: its gate, the highest slot, slot
    0 beside 2, none for a self-target; IDA, actor-ai-and-copies.md). 18 of 30 flows held a lasting raw slot difference at a
    check and passed both runs, the three kill moves among them; `kill-moves-cross` shows a real case, 1 of 2 (bugs.md,
    Headtracking on copies). A review then found the first mirror of the engine wrong (life states 4, 5 and 8 read, the settings
    behind its range, the scene behind alternation, three conditions missing) and a scenario passing on `"unread"`. Reading on found
    more: life 4, 5, 8 ignore the slots after all (`Actor::GetPathLookPosition` 37851), the player has its own gate, and only
    `BlockAnticipate_*` read the slots from the graph. Corrected, the 18 and `kill-moves-cross` pass.
  - **A crash found on the way, ours:** a copy's own do-nothing package had procedure type -1, which the engine indexes a table with
    unchecked: `GetCurrentAIProcedure` on a player's copy crashed the game every time, a released copy on its next load. Fixed
    (`CopyPackage::CreateDoNothing`, `copy-answers-its-ai-procedure`, GameTests `[copy-package]`). That left a released copy
    standing frozen on the package; a release now ends it (`released-copy-takes-up-its-ai`).
  - **The lock checker's list** held 8 of 10 pass-through hooks, so the engine's own orders failed flows as ours. Now 32, with a
    critical log and a harness unit (lock-order.md). ClientTests and GameTests `[headtrack]`, harness units.

2026-11-06: the bugs mapped ([bugs-map.md](bugs-map.md)): shared causes, wrong checks and an order. Most of the drift
detector's "lasting" reports are its frozen snapshot, not drift (new verbs `graph_watch`, `hands_compared`). New entry: a
copy without its weapon, the user's report, not reproduced.

- **Owners never moved without walking: the check read culled skeletons** (2026-11-05). The host's Riverwood NPCs the
  gait check flagged (`actors-walk-after-arrival`, 0 of 6) were off camera: the engine does not pose a culled actor's
  skeleton (`HighActorCuller` 40335 sets the 3D's `kNotVisible`, `ModifyAnimationUpdateData` 38054 then writes no
  pose), so its feet hold still while it walks, and the `was_in_frustum` the check trusted goes stale (only
  `ProcessTracking` writes it). `GaitWatch` now counts drawn steps only and the observer's pose comparison reads the new
  `pose_visibility.drawn`: the scenario passes 3 of 3. Read on the way: how the movement controller switches to the
  Virtual Path Follower for middle-high actors (`actor-movement.md`, both new sections). New: the `ref_steps` verb (with
  the vanilla stand-in's `ref_trace`), `gait_after_arrival`, and root, feet, cull, process level and movement state in
  the animation trace. Tests: ClientTests `[gait]`, `test_world_diff` (a stale `was_in_frustum`).

- **Names (2026-11-05):** names go through the host and each game applies them one way. Spawns carry `DisplayName`, a player's too
  (the host's copy showed ""); a rename or a host's load of another character sends `PlayerNameRequest`; the host's game
  sends `NotifyActorName` when a quest alias sets or clears an NPC's name. This replaces the resolved-name decode. Checks
  compare players' names now. What is covered: `docs/re/systems/names.md`.

- **NPC copies no longer vibrate in furniture** (2026-11-05). On the guest's game, copies going in or out of a chair, bed
  or wall lean shook 0.6 to 1.8 units each frame: `Actor::Move` put them on their goal, then the engine's write-back moved
  them off it. The engine turns an NPC's capsule collision off in furniture from the AI copies never run; copies now do
  the same: a copy's capsule collides when its owner's does, sent with its movement (`actor-movement.md`, "A copy in
  furniture"). The clocks and sky measured
  clean; the lights the user saw were not reproduced. New: the `jitter` MCP tool, `JitterWatch`.

Newest first.

2026-11-06: why the player's paths are refused, read in IDA ([pathing.md](re/systems/pathing.md)): `PathToReference`
returns false when `SetGoal` refuses the request or the navmesh search fails. A new harness read, `path_probe`, runs
those steps without moving anyone: both ends' triangles, the search, and whether the navmesh's triangles join them.
`walk_to` and `fight` print it on `no_path`. Inside Embershard Mine both ends are on one navmesh, but in 7 runs of 10
its triangles don't join the entrance to the bandit, and the same save joins them in the others. That points at
a navmesh obstacle: Skyrim.esm's CollisionMarker 105C99 cuts the ramp down (bisected by disabling obstacles; NPCs refused too). A game without a session cuts it the same (3 of 3): not ours. The marker is an invisible wall
for the player too, so `clear` now fights only who the engine's search reaches, nearest along the way (four bandits
skipped), and the flow runs to its end; `loot` and `leave` show the filed bugs. The flow's three bugs went to `bugs.md`.
A `terrain` readout ([terrain.md](re/systems/terrain.md)): the ground around a spot as a walking player meets it,
a column of rays a cell over frames (the pick budget), the navmesh's pieces, drawn top-down and in profile (MCP
`terrain`; crossing and `fight` failures carry it). `walk_to` crosses a navmesh cut by input (`cross`: swims, jumps
a low wall) where a player could.
A full run with the readout (after PR #29): `talk` follows an NPC walking out of 3D reach, `walk_to`'s time comes from
the engine's path length (`path_probe` `path_length`), the readout ignores actors, and the lookout trip goes in and out
by the mine's door (a moveto left the party in a nook). The search failed from a spot down the mine's tunnel in some loads (pathing.md, not reproduced since):
a refused walk now runs the first stretch of the search's path or the triangles' way by input, then paths again.
The whole flow runs: every step PASS but lookout and loot, SEEN with filed bugs (hands, corpse items).
`DuoLock.acquire` now checks its hold is still listed: an MCP session whose hold another command freed ran on as
holder, and the harness refused its commands ("holds no duo"). A running MCP server picks the fix up at its restart
(`duo_lock` stays loaded across `mcp_reload`).

2026-11-05: playthrough flows ([playthrough-flows.md](design/playthrough-flows.md)): `coop_test.py flows` plays a real
stretch of the game as ordered, checked steps from a save, with checkpoints (`--from STEP`) and a summary per run; a
copy difference players see marks a step `SEEN` and the flow goes on, cosmetic ones are listed only
(`comparisons.classify_problem`). The first flow, `riverwood-day-one`: A Lovely Letter start to reward, the Trader's
door, Embershard Mine's lookout, door, first room and loot. New verbs `door`, `clear`; `use`/`talk` check the crosshair
first. Found: a looted corpse keeps items on the guest (5 of 5), the host's copy sunk in the floor after a door,
players stacked by a door's tether arrival. Open: the player's paths refused inside Embershard Mine.

2026-11-05: drift detector for copies' graphs (`Games/Animation/GraphDrift`): after each replay a copy takes, its active
  state names against the action's `GraphState` (SaveState 63601's machine section, hkbCharacter 65148), counted as
  matched, transient or lasting (open past 2 s), players' copies and paired actions left out; a `copies' graph drift:`
  line per flow in `coop_test.py scenario`. A 12-flow animation batch: 283 NPC replays, 261 matched, 12 transient, 4
  lasting (animation-replay-recovery.md, "Drift detector"). Each lasting one is a missing input, listed in bugs.md; no snapshot loading over it.
  ClientTests and GameTests `[graph-drift]`. Replaced 2026-11-06 by a live read: those lasting counts were not drift.

2026-11-05: `bugs.md` audited and split into sections an agent can take whole, each with its task, size and what it
waits for. Closed: the three risks of 2026-10-31 ([report](reports/2026-10-31-v1-three-risks.md); its two unrelated
failures stay as entries), the freezes (now a loose end), and host objects waking and resting every half second. For that
last one, the entry's check on the 2026-11-04 play session's host log: 79 `HostPhysics: sent N rests` lines in 8 minutes,
136 bodies, at most 14 rests of a body with gaps of 1 to 122 s, none of them the twice-a-second loop (fixed 2026-11-01).

2026-11-05: the 2026-11-04 freezes are the machine's, not ours (Loose ends, Engine threading and crashes). Fixed on the way:
the guest's `BodyService::Apply` copy scan, `profile --stacks` crashing under Wine, lock-order `[ours]` on pass-through
hooks. New: `profile --route` and `--budget`, against the vanilla stand-in.

2026-11-05: IDA work reaches the repo by itself ([re/README.md](re/README.md), The journal). Hooks in
`.claude/settings.json` (`scripts/re/ida_hook.py`) journal every function read and every name, comment and type
written through ida-multi-mcp into `docs/re/journal/<version>/<branch>.tsv`; `build_re_data.py` reads the journals and
`ida_replay.py` rebuilds a database from them. They remind, never block: when unnamed functions pile up read and
unrecorded, and when the duo or engine code comes before any IDA read. The one refusal: an IDA instance with another
database open (work there was lost). `scripts/re/id.py` maps IDs to addresses; `check_re.py` (in check.sh) catches
added note rows with a bad ID or RVA and IDA auto names in the notes and systems docs. The shared database moved to
the main checkout's `build/re/SkyrimSE_1.7.104.i64` (it had been a party-markers copy). Units in
`scripts/test_ida_hook.py`, `scripts/test_check_re.py`.

2026-11-05: a guest's crime flags the whole party, and the NPC goes for the nearest player ([crime.md](re/systems/crime.md),
the user's rule in `party-model.md`). `HostAggro` counts a crime-hostile NPC's fight as a party fight, so `PartyThreat`
picks by distance; a copy had no greeting pass keeping detection fresh, so `CrimeService` holds a guest's theft 0.5 s,
asking. Also found: the party's first two seen thefts only warn (`TheftWarnings`). Scenario `guest-theft-aggro`
(replaces `guest-theft-witnessed`, which passed on a stale sighting); flows in `crime_flows.cpp` and
`party_aggro_flows.cpp`. The older crime scenarios' failures are in `bugs.md`.

2026-11-04: the party's bounty works on 1.7.104 ([crime.md](re/systems/crime.md)). `CrimeEngine::Player` kept 1.6.1170's
offsets, so the bridge read the bounty map 8 bytes off: the engine wrote bounty, nobody saw it, no `NotifyCrimeState`
went out. That was the 2026-11-04 playtest's missing bounty. MCP: `own`, `witness`, `witness_faces`, `wait_seen`,
`party_bounty`, `bounty_total`, `expect_bounty`, `expect_target`, `report_crimes`; harness `crime knowledge`, `crime owns`, `reporting`.
Scenario `guest-theft-witnessed`.

2026-11-04: the repro MCP on a real bug and with checkpoints ([repro-mcp.md](design/repro-mcp.md), "Used on a real
bug"). The kill-move redo through it found crossed kill moves still kill a player a second later (`bugs.md`, Combat;
for PR #13). `checkpoint`/`retry` roll the party back to a host save in 3.9 s instead of a new duo (the harness's
`game_loaded` engine event). Built on the way: nested traces sharing the recorder, `expect stays`, player handles after
a respawn, brief `clips`, big results to a file. Units in `scripts/test_coop_mcp.py`.

2026-11-04: the repro MCP grows instead of being left ([repro-mcp.md](design/repro-mcp.md), "Robustness"). The kill-move
work left it for scratch scripts (rounds, a trace filter) because its code loaded once. Now `mcp_reload` loads the
harness again keeping the session, `verb(name, args)` calls any Session method (recorded, exported as `ctx.<name>(...)`),
`rounds` repeats a call with a line each, `trace_start`/`trace_read` show a filtered animation trace, `log` greps the
plugin logs. A bare `Session` that drives the games without the duo is refused (`Game.rpc`); `scripts/check_scenarios.py`
(in `check.sh`) fails a new scenario that says neither `# recorded:` (stamped by `record_export`) nor `# hand-written:`.
Python units: `scripts/test_coop_mcp.py`. Checked on the duo, the exported scenario replayed. Also fixed: a fresh worktree's
games stopped at a CrashLogger dialog (the game's user couldn't write `My Games`; `prepare_prefix` grants it now).

2026-11-04: copy state from Riverwood, both fixed. Level: a placed leveled NPC's copy (a Whiterun Guard, 1000 against
1; Odahviing's 20 against 1 the same kind) was given the owner's face record, dropping its own game's resolved base and
its stats; it now keeps that engine-made base when its face matches the owner's (both games load one save), and reads
the same level on the duo ([leveled-npc-copy-names.md](re/systems/leveled-npc-copy-names.md), "A copy keeps its game's
resolved base"; GameTests and ClientTests `[copy-level]`; `leveled-placed-npc-is-hosts` now compares level, and passes
it, then fails its cleanup: the guest loses its own guard reference once the host sends it home, on its unchanged
permanent-base path). Open: a different face, and created copies, still take the face record's
stats. Hands: a copy's move to another cell drops its hand spells, and the equipment snapshot compared only worn items;
it now compares the hand spells too ([copy-hand-spells.md](re/systems/copy-hand-spells.md); scenario
`copy-keeps-spell-after-party-travel`, GameTests and ClientTests `[hand-spell]`).

2026-11-04: a container the host creates reaches a guest with what it holds (`bugs.md`, Containers, deleted). Each
create row of a container is followed by its contents, rolled on the host first (`AddCreatedContents` in
`HostWorldState`, for object changes, cell states and a wait's world; `HostGame::ReadContents`); the host's later item
changes to a created container go as its whole contents (a guest rolls no world container, `HookInitLeveledItems`).
`HostAuthorityTests` for the three call sites; GameTests flow "A chest the host's script places reaches a guest with what it
rolled" (the fake learned created containers); scenario `created-chest-reaches-guest-empty` left to the next combined run.

2026-11-07: the follower that stops following is marked not reproducible on 1.7.104 (a party reload, a guest's death and
respawn, a follower left behind then moved in, a load door, a wolf fight), and no fix is made. Read in IDA instead
(`re/systems/followers.md`): the follow procedure ends on a dying, dead, deleted or other-interior target, its load drops
a temporary target, and the package pick never restarts an unchanged package, so only our target-map restart brings it
back. New entries in `bugs.md`: a guest's recruitment stalls after the host takes the pick, and a guest crash on a
deleted copy in an AI job (39372).

2026-11-04: the follower that stops following is not reproduced on 1.7.104 (0 of 2 in the entry's order, with the Load
menu and door rounds), and no fix is made; findings in `bugs.md`. The 1,388 lead is closed: she stood by the guest she
follows. `load_latest_from_menu` works on 1.7.104 again: it passes the character list and clears a reload's scratch save.

2026-11-03: the repro MCP plays toward a bug ([repro-mcp.md](design/repro-mcp.md), "The play flow"): `scene` (a place, a
pinned clock, named NPCs, what already differs shown), goal verbs with one outcome and a named `why` (`go_to`, `talk`,
`use`, `take`, `fight`), `look`/`observe` diffs/`why`, new `expect` kinds, a spec draft with leftovers, the `scene`
fixture. From `bugs.md` alone: the created chest reproduced and exported (`created-chest-reaches-guest-empty`, fails
until fixed; its GameTests flow lands with the fix); the follower entry not reproduced. `faendal-letter` runs through
the runner and fails only its end comparison on real desyncs. Seen in Riverwood: a Whiterun Guard's copy level 1 against
1000, the player's copy `state.hands` empty against 77772 (both fixed 2026-11-04, above). Python units in `test_coop_harness` (GoalVerbs, Scenes).

2026-11-03: dialogue lag on a guest (Orgnar, 30 s after each pick). Two causes. A guest's press through a line never
reached the host's game, which said it to its end: now `DialogueSkipRequest` / `NotifyDialogueSkip`, from anyone in the
conversation, and every copy goes quiet with it (`dialogue-guest-skips-line`, GameTests, HostAuthorityTests). The 30 s
itself came from the duo having no audio driver: a voice line was timed from its text, about twice its spoken length.
On its null audio device Orgnar's alchemy answer takes 16.1 s, was 36.8 s ([how long a line lasts](re/systems/dialogue.md)).

2026-11-03: the duo's games have a silent audio device again. Running as `coop2` (2026-11-01) they had no audio
driver, so voice lines were queued and never started: `dive-host-player`, `dive-client-player` and
`copy-ends-voice-line` failed every time. Wine's ALSA driver now plays to ALSA's null device
([testing.md](design/testing.md)). `copy-ends-voice-line` passes; both dives now pass their gasp and fail later on
the open "Headtracking left over" (bugs.md). The dive's surfacing step names what it saw when it fails.

2026-11-03: launcher Play co-op / Bethesda graphics settings menu, invitation bypass, `/perf` and per-connection
network diagnostics, and verified save-block reuse for world joins. Details and current validation:
`docs/design/network-performance.md`; launcher preservation: `docs/install.md`.

2026-11-03: the duo manages itself (`scripts/coop_harness/duo_lock.py`, testing.md "The duo lock"): an flock'ed
FIFO queue every duo command and the MCP wait in, freed when the holder's process ends (a keeper holds games left
running), stale holders taken over after their games stop; `DUO.md` is a generated view, `scripts/duo.py status|run`.

2026-11-05: a queue place whose owner stopped calling no longer blocks the queue (four sessions sat 10+ hours behind an
idle head with the duo free): it lapses after 5 minutes without a poll, `duo_stop` leaves the queue,
`scripts/duo.py leave` drops one by hand.

2026-11-03: `walk_to` runs (repro-mcp.md, Phase 3's speeds): `PathToReference`'s speed only scales the AI-driven
player's walk (0.5 was 40 units/s), so a walk now runs after an actor with `KeepOffsetFromActor` (300-450 units/s)
and walks a `PathToReference` leg only round what stops it; `steer_to` turns Always Run on (status
`player.always_run`) and sprints. Faendal flow 252 s to 113 s. Python units in `test_coop_harness.Repro`.
Open: the runner's baseline refuses `LateAfterDragonslayerBackinSkyrim` (Odahviing's copy: level 1 vs 20, fixed
2026-11-04 above; 300 units off); `stage` put a staged NPC 75,000 units off after the players had hopped cells.

2026-11-03: a jumping player's copy no longer stutters in the air. Since copies are stepped by their engine
(2026-11-02) a copy stayed on the ground while its owner rose, was placed past 64 units off and fell by its own
gravity, twice a jump. The owner now sends `Movement::OnGround` (its controller on the ground) and a copy is stepped
only then: placed on its path from take-off to landing and in any other state (swimming, climbing, flying, ones not
met yet), its height held when a snapshot is late
([actor-movement.md](re/systems/actor-movement.md#a-copy-in-the-air-2026-11-03)). Scenarios
`copy-jumps-smoothly-host` / `-guest` (failed both ways, pass); GameTests "A jumping player's copy rises and falls
with it, never stuck on the ground"; ClientTests `[copy-step]`.

2026-11-03: an actor landing on the party's arrival spot passes through the players. Stepped copies never yield, so
Lydia's copy landing on a guest shoved the guest through a table beside the door; any NPC's copy put on the spot now
joins the arrival stack for its first 5 s, until it walks off ([actor-movement.md](re/systems/actor-movement.md),
"A stepped copy shoves a player it lands on"). Scenario `follower-lands-on-guest`; GameTests travel flow; ClientTests
for `ArrivalStack` and the collision groups.

2026-11-02: smarter enemy aggro is reviewed and merged into `dev`: recent applied damage and
proximity choose between party players, with a target hold and switching margin. Copied combat targets now persist
through native updates. Steam 1.7.104 acceptance passes all six targeted scenarios, including both attack orders,
300-frame stability, guest combat state and casting; the combined fast gate passes 202 client / 199 host / 33 encoding /
290 game cases. [Design](design/party-aggro.md).

2026-11-02: party markers (playtest R2) are reviewed and merged into dev: cyan compass
icons and named pins on the world/local maps, following displayed player copies and clearing on departure.
The combined fast gate passes (193 client / 198 host / 32 encoding / 282 game cases); all five targeted native
cases pass on both Steam 1.7.104 and the retained 1.6.1170 build.
[Design](design/party-markers.md).

2026-11-02: `guest-throw-lands-on-host` failed 2 of 3 at its grab, fixed in the scenario. It teleported the client
beside the ladle and pressed at once, while its copy was still reaching the host. The host's ladle could stir then,
and its frames keyframed the client's copy mid-grab, which the engine refuses (by design: a body the host's frames
move is the host's). Waiting 0.5 s first, the grab took 8 of 8 (probe). The scenario now waits until the ladle is
still in both games and the copy has arrived (`ctx.wait_until_settled`), then grabs with the checked `ctx.grab`: 5 of 5.

2026-11-02 (`3b8124d0`): copies step through the engine (`actor-movement.md`, "Copies step"), and a held body
held still is not at rest (`physics-sync.md`, "Held still is not at rest").

2026-11-02: the repro MCP ([repro-mcp.md](design/repro-mcp.md)): `scripts/coop_mcp.py` keeps one harness
session open over the duo, so a bug is staged step by step: a frozen world stepped a frame at a time (`Main::freezeTime`,
`TestHarness/FrameStepper.h`), `stage`/`drop` with handles per game, `observe` both games, `expect`, and
`record_export` writing the session as a scenario (`scenarios/recorded/`) or a shared spec. Python units
(`test_coop_harness.Repro`), ClientTests `[frame-stepper]`. Phase 2: named counters (`Diagnostics/Counters.h`, in
status and `profile`), round trips and clock syncs in status, press timing against the vanilla stand-in. Phase 3:
`walk_to` along the navmesh (AI-driven player, `PathToReference` in legs), `talk_to`, `pick`, `menu`, `travel`,
`quest_walk` (the late-save duo boots and connects again). A quest played by the verbs on a late save ("A Lovely
Letter": walk to Faendal, talk, three picks, stage 10 in both games) records and exports with its save
(`needs_save`) and replays. Open: the guest's engine refuses paths to NPC copies it spawned (`walk_to` walks straight
then); on `LateAfterDragonslayerBackinSkyrim` the summit's dragons (Paarthurnax, Odahviing, two more) are registered by
neither game (session id -1 in both): they stand in an exterior cell the engine made at load (`FF000A15`, no plugin
record), and `BuildAnnouncement` (and `ReadWhereabouts`) dropped an actor whose cell has no session id. Fixed
(2026-11-03): outdoors such a cell goes as none (`ToSessionCell`, FormIds.h) and the copy is placed by worldspace and
position (`TravelService::CellAt`); ClientTests `[FormIds]`, GameTests "The host's NPC in an exterior cell the engine
made gets a copy on the guest" (failed before). On the duo the dragons now have session ids and copies. The runner's
baseline check there then stops on two newer differences: Odahviing's copy was level 1 against the host's 20
(a copy took the face record's stats; fixed 2026-11-04, above), and, open, his copy trails him in flight by ~300 units
(interpolation delay at flight speed against the walkers' tolerance).

2026-11-02: one writer for a loaded body's rest, and a guest's movement sent unreliably (red flags 4 and 5): a live move row no longer places a body the host's body rest already placed (a script's move still
does); `ReferencesMoveRequest` goes unreliably unless it carries actions or a teleport, as the host relays it (body
frames stay reliable). GameTests "A loaded body takes its rest ..." and "Over a lossy line a guest's player reaches
the host without stalls" (5% loss: the longest wait for its next snapshot 65-98 ms, was 127-323); TPTests
`[encoding.delivery]`. Flag 3 measured and left: 2 frames from a press to motion as host, guest and vanilla.

2026-11-02: the host passes a guest's movement stamps on as sent (red flag 2): it clamped them to its own
time, 24% of a guest's snapshots, by 9 ms on average and up to 179, which made the gaps uneven; `CopyClock` never plays
ahead anyway. HostAuthorityTests "Movement: a guest's stamp reaches ..."; GameTests "A guest's player moves evenly on
the host's game though its clock runs ahead".

2026-11-02: actions are stamped with the frame clock, as movement is (red flag 1): with the session's
time as read a copy played an owner's action up to 15 ms off its movement, unevenly; now a steady 11 ms early
([red-flags.md](design/red-flags.md)). GameTests "A player's actions play on its copy where its movement was when it
acted"; the fake's engine frames can be steady while our update lands unevenly (`FakeEngine::FrameMs`).

2026-11-02: a held object no longer flashes green on the watching game (S2, merged `eb913f95`). Manual scene updates
ran in the Papyrus VM job; body playback, left-behind node repairs and received object placements now run between
frames. The native tankard detector failed before the fix (18/340 sampled frames) and passes after it (0/2,773
across three observer checks); throw and push controls pass. Three GameTests guard scene-write timing; four unit cases
cover deferred repair/rest bookkeeping and cancellation. The guest watching the host gives the native before/after
proof; the host-watching-guest controls were negative before the fix too.

2026-11-01: dialogue follow-up (merged `edca669f`, with the S3 arrest scenarios): refused choices restore their list,
competing clicks display the host's accepted text, and barter resume re-arms input (the three `bugs.md` Dialogue
entries, deleted). Native failures, failing GameTests and unit tests preceded each fix. Six coverage improvements
include stricter fake input, pick-completion postconditions, duplicate/competing choices, stale notifications after
reopen, same-topic mouse INFO selection, and barter/training returns. Full check passes; native fixes and exit
controls pass. A separate queued Lydia greeting still blocks `dialogue-pick-reaches-host` before any pick (bugs.md).

2026-11-01: a guest can take a chest's fresh roll (S5): the host's game found none of a freshly rolled
leveled item to give (its read counted the changes' 0, not the engine's count through the base's leveled list) and
removed none from its list (`ExtraLeveledItem`); it worked after a load. `HostEngine::TransferContainer` now counts
with the engine (16108) and takes from the matching list ([inventory-changes.md](re/systems/inventory-changes.md),
"A fresh roll"). Scenario `guest-loots-hosts-roll`; no GameTests flow (below the bridges the fake replaces).

2026-11-01: other players move smoothly (S10): the copy clock and owners' snapshot stamps stepped with the
session's time as read once a frame, which jumps (27 then 6 ms) while frames are steady, so copies stuttered. Both now
step by the engine's frame (`FrameClock`, [copy-clock.md](design/copy-clock.md)); players and fights are sent every second
frame at 60 fps (were every third), and a copy's strafe animation turns with its movement (was a snapshot late). Scenarios `copy-strafes-with-host`,
`copy-strafes-with-guest`; ClientTests `[copy-clock]`. The vanilla stand-in records the player's path
(`player_trace.on`) to compare a player's own movement.

2026-11-01: clutter no longer creeps above 60 fps (S4): vanilla Havok steps by `fMaxTime` (1/60)
whatever the frame rate, and a step longer than the frame wakes resting objects. Each frame's step is now the frame,
as HIGGS and HavokFix do ([physics.md](re/systems/physics.md), "Havok's step"). Scenario
`clutter-rests-at-any-frame-rate`; `ClientTests` `[havok-step]`.

2026-11-01: a joined player can change its armor again (S7): the join put the kept character's worn set
back on force-equipped (`abForceEquip`, Papyrus `EquipItem`'s prevent removal), so the inventory menu could not take
it off or replace it ([inventory-changes.md](re/systems/inventory-changes.md), "Worn on add"). Scenario
`guest-swaps-armor-after-join`; no GameTests flow (the call is below the bridges the fake replaces).

2026-11-01: a guest's held object is steered on the host, not keyframed: the host's physics stops it on
an NPC's head and the other guests get the host's result (S1, [physics-sync.md](design/physics-sync.md), "Steered
holds"). The host's physics matches a game without a session again: the host wrote every awake object's 3D each
update, which re-placed it and lost its contacts (a dropped bucket rested 21 units deeper on a head); now only when
the engine left the 3D behind ("Matches a game without co-op"). The bucket trick works for either player
(`guest-bucket-trick`, `host-bucket-trick`). The duo's games ran with Wine's audio drivers off then (they took this
user's sound card; a silent device since 2026-11-03, above).

2026-10-29: party dialogue built: every menu shows every player's options tagged with who can say them,
one shared highlight, a player says only its own options ([shared-dialogue.md](design/shared-dialogue.md)).
Fast tiers and the Skyrim dialogue tag pass (the party list, the shared highlight, a question answered by the guest).

2026-10-29: the v1 list ([v1-playtest-todo.md](v1-playtest-todo.md)) gained first-time join (Quick Start) and a
save/load gate; the werewolf crash moved to `docs/bugs.md` (after v1). The party limit is a Steam lobby's 250 (`PartySize.h`,
was 4 on Steam, 8 on the host); only up to 4 players have ever been tested.

2026-10-29: bug reports from play: `/bug`, `/export`, crash bundles at the next start and `coop_test.py report` ([bug-reports.md](design/bug-reports.md), [playtest.md](playtest.md)); the harness status and log trail now come from the shared `Code/client/Diagnostics/`. Fast tests pass; the plugin builds (test and player builds). `bug-report-writes-every-game` passed in Skyrim before screenshots were dropped and saves kept once per export; that change awaits a rerun on a free duo.

2026-10-28: the party-scripts-2 and v1-session-items work, and crafted potions (v1 checklist), are integrated into dev.
The combined fast tests and Windows plugin build pass. Review findings and verification limits:
[integration review](v1-integration-review-2026-10-28.md). Agreed remaining work:
[v1 checklist](v1-playtest-todo.md). Worktree references below record where each feature was built.

## Direct authority migration

Porting Skyrim Together's server to the host's game (`docs/design/host-authority-migration.md`).

| Phase | What | Status |
|---|---|---|
| 0 | Ground truth: message table, backbone scenarios, harness `disconnect`/`game` status | done (`ab302206`) |
| 1 | The host talks to the embedded server in-process, no socket | done (`a76aeddd`) |
| 2 | `HostBridge` (now `HostAuthority`), movement relay, late join from live state | done (`e49e7aea`) |
| 3a | Relay families as bridge rules: inventory, actor values, magic and combat, character extras | done (`84fb7465`, `c5936751`, `cd3dfd0a`) |
| 3b | Character ids, owners, spawns, removal, respawn on the bridge | done (`5e99eae7`) |
| 4 | Host-decided state | partly, see below |
| 5 | Delete `Code/server/`; the enet6 listener moves into the bridge | done 2026-10-21: `HostNetwork` (below) |
| after | Physics sync on the bridge's unreliable channel | objects and ragdolls live from the host, a guest's pushes, casts, grabs and drops to the host, 2026-10-24 (`docs/design/physics-sync.md`, steps 1-6) |

### Phase 4 parts

| Part | Status |
|---|---|
| Time (`HostClock`; set-time from any player) | done (`4e0efb0a`); set-time works in the game since 2026-10-20 (`/settime` in the chat, `guest-sets-time`); the whole calendar 2026-10-20: a guest takes the host's date and days passed too, written as `Calendar::Update` leaves them (`docs/re/systems/calendar.md`) |
| Weather (`HostWeather`) | done (`50397fa0`) |
| Host/guest code split, the world (2026-10-20) | done: `Services/Guest/` (`GuestClock`, `GuestWeather`, `GuestWorldState`) exist only while joined (`GuestService`); `Bridges/HostEngine` (the host's reads, set hour) and `Bridges/GuestEngine` (applying the host's calendar, weather, globals, quests) replace those `IWorldBridge` calls; `CalendarService` is `TimeScaleService`; `WeatherService`, `WorldStateService` deleted; `HostBridge` renamed `HostAuthority`. Actors next, by owner/copy (`docs/ideas.md`, "Client services by role") |
| Session = party: roster, host kicks, no party messages | done (`50397fa0`) |
| Cell arrivals (party tether: copies dropped and resent on teleport) | done for every travel: `coc`, doors, fast travel, moves all end in ID 40438 (`docs/re/systems/player-travel.md`; door and fast-travel scenarios `87972601`); the arrival refreshes the host's record of the player (inventory, values, hands) |
| Arriving together: the party stacks on the arrival spot, passing through each other until each walks off; followers face the leader's way, so every game shows the same view; an NPC follower or copy landing on the spot joins the stack (2026-11-03: her copy shoved the guest through a table) | done (replaces the step-aside of `83178416`), `party-stacks-on-arrival`, `party-uses-door`, `follower-lands-on-guest`, GameTests "A follower landing on a guest's arrival spot...", ClientTests `[arrival-stack]` |
| Copies converge on their owner (presence): a copy whose owner is around but which is elsewhere, without 3D or deleted is moved or spawned again; a spawn for a known copy refreshes it | done (`87972601`), `party-fast-travels` |
| Players' copies travel with the party: persistent, moved with their owner, never respawned on travel | done (`df9af2ff`), the travel scenarios |
| Experience: personal, earned by whoever did it; party rewards to everyone (`docs/design/party-model.md`, "Experience") | rule decided 2026-10-19; wiring below. quest rewards built 2026-10-27 (`design/quest-rewards.md`); measured combat XP sharing removed |
| Objects: activation, locks, script animations | done as bridge relays by form id (`31abfd23`, `550ece84`); the unused object ids deleted (`b7b83aad`) |
| Shared dialogue: one conversation for the party, run by the host's engine; each player sees their own options, anyone picks with their own line | parked 2026-10-19 until quest/world sync and AI state on copies. In: the host's rule and messages (`HostDialogue`, `dialogue_tests.cpp`), copies' own lines and subtitles, the engine structs (`MenuTopicManager`, `UIMessageQueue`). The client (`DialogueService`, `DialogueBridge`, harness `dialogue`, `dialogue_pick`, `skill`, 22 scenarios passing on 2026-10-19) is on tag `archive/shared-dialogue-client` (was a branch): it syncs the host's list to each peer, to be redone as step sync (below; `docs/design/shared-dialogue.md`, "Next"). Engine notes `docs/re/systems/dialogue.md` |
| Quests | done 2026-10-20 as world state: the host runs every quest, guests take its stages (fragments skipped), objectives and alias fills (`docs/design/world-state-sync.md`, `docs/re/systems/quest-apply.md`); upstream's quest relay deleted with phase 5 |
| Waypoints, chat | done as bridge rules, server services deleted; chat is an RmlUi box (Enter), `chat-reaches-everyone`; player list health % and dialogue lines in chat dropped (`b7b83aad`) |
| Shared map markers: a marker placed on the map (P) shows on everyone's map | on again; placing and removing (the map's "Remove It", by mouse), `map-marker-reaches-everyone` (`b7b83aad`) |
| Teleport command, teleport to a player | `/tpa` (or `/teleportall`, 2026-11-09): anyone brings every other player onto their spot (stacked, as a door's arrival), a forced tether move, from the same cell too (`player-travel.md`, "/tpa"; `party-summon-in-cell`, GameTests `/tpa ...`) |
| Tether itself: anyone who travels takes the party along | done (`df9af2ff`): the leader's arrival moves everyone else there, the host too; `party-uses-door`, `party-fast-travels`, `party-quick-travel`, `load-door-not-replayed`. Since 2026-11-08 every teleport, inside the cell too (PayFine's release; `docs/design/party-travel.md`, "Every teleport"), `party-teleports-in-cell` |

### Experience: wiring up

The rule and why: `docs/design/party-model.md` ("Experience"); engine path and the mods:
`docs/re/systems/experience.md`. Each step with its scenario. V1.0.0 uses the native game test setup;
installed-mod kill XP and multiplier compatibility are later work (`docs/ideas.md`, "Experience mods").

1. **Drop upstream's shared combat XP**: done 2026-10-27 with quest rewards. No measured gains leave a game;
   the relay and receiver, LastUsedCombatSkill and ScopedExperienceOverride/27244 are removed. Reserved legacy
   wire slots remain. `xp-stays-personal` replaces `shared-xp`.
2. **Fix `HookAddSkillExperience`'s signature**: done 2026-10-27, checked against CommonLib and disassembly:
   advance object and action forwarded. It emits only PlayerProgressEvent for character records.
3. **Speech from a picked line to the picker**: done 2026-10-22 by rule 2 (the line runs for its picker, whose game
   runs its `Game.AdvanceSkill`; `dialogue-picker-gets-speech-experience`), replacing the 2026-10-19 reroute. A
   script's skill gain outside a picked line (a quest's reward) is the party's (step 6).
4. **Barter Speech to the trader**: done 2026-10-27, with barter on the picker's game. Host/guest real purchases
   raise only the trader's Speech; dialogue closes before the barter menu takes input. Fast routing/flow tests and
   native evidence: `design/native-bugs.md`.
5. **Training to whoever pays**: done (2026-10-19; by rule 2 since 2026-10-22, `Game.ShowTrainingMenu` running on
   the picker's game): the cost, the skill and the trainings this level are the picker's (`dialogue-picker-trains`).
6. **Quest rewards to everyone**: built 2026-10-27 (`design/quest-rewards.md`), as the party's calls. Skill input,
   explicit skill levels, perk points, perks and permanent stat grants reach every present player's own engine.
   Real host/guest quest turn-ins check skills, perks, picker-only gold and native guest completion/stage events.
   Experience DLL itself is not installed/tested; its game's native event delivery is measured.

### World sync: finishing it (plan 2026-10-21)

Before AI state and dialogue (below), world sync is finished while its pieces are fresh (`docs/design/world-state-sync.md`,
"As built" and step 7). Each step ends with its scenarios and the oracle (`world-diff`, `world-changes-reach-guest`) passing.

1. **The world diff after every scenario.** `scenario` compares the two games' loaded worlds after each scenario
   (`world_differences`, idle animation skipped) and fails one that leaves them different; `coop_test.py unsynced`
   collects the flags with no row from the whole suite. Expect failures at first (the travelling scenarios);
   each is a sync gap, a row or a scenario that leaves the world changed.
   Built 2026-10-21 (`check_world`): after each scenario, the cells both games have loaded, a difference counted
   when it lasts 3s; again at home after the reset (charged to the scenario that left it, not the next). `objects`
   and `cells` pass but `create` (step 3); `party-stacks-on-arrival` fails on its own check (the client walks
   through the host's player), unrelated. Found on the way: a model's own loops (QASmoke's spinning arms) frozen
   off-camera at different phases (pose not compared on references neither game changed and with no graph); the
   lock kind's put-back could not reach level 0 with the console (harness `lock` now). `unsynced` adds base
   object `0x80` (E95D1, arriving in SoljundsSinkhole01) and encounter zone `0x20000000` (664DE, through door
   13419): both on a cell's first load, likely leveled rolls, step 2's.
2. **State at cell load.** A guest's cell loading gets the host's current state of every changed reference there,
   with the same rows (`ReferenceStates`): the changes a guest missed while away (dropped today, "its object is
   not in this game") and leveled containers (containers slice 2: the host's roll, not the guest's). A scoreboard
   kind: the guest leaves, the host changes something, the guest comes back.
   2a, the references, built 2026-10-21: each cell a guest loads (`TESCellFullyLoadedEvent`, and every loaded
   cell on joining) sends `CellStateRequest` with the references its own game holds row changes for; the host
   answers with every row of the references either game changed there (`NotifyObjectChanges` with `Cell`), or
   once its game has that cell loaded. A cell's state skips what already matches (no events); live changes
   still apply as the host's game did. Scenario `late-join-gets-cell-state` (fails on the old build, passes
   now); bridge cases in `world_state_tests.cpp`. Found: a cell's references stay in memory after it unloads, so
   live changes already reached cells a guest had loaded once; the gap was cells it never loaded and changes made
   while it was away. 2b, leveled containers, built 2026-10-21: the host rolls each container of a guest's
   loaded cell (16038) and sends all it holds (`kContents`); a guest's game rolls no world container's leveled
   entries (16129 skipped). Scenario `guest-opens-hosts-roll` (fails with the two lines off: the client's own
   roll). 2c, placed leveled items, built 2026-10-21: a reference placed with a leveled base (`XLIB`, e.g. Soljund's
   Sinkhole's clutter books: `DummyBook` `D7866` with `LItemBookClutter` `D9F89`, 124 books) rolls the list once at
   its cell's first load in `TESObjectREFR::PickLeveledBase` (20232, our name; called from the cell's reference
   loads), takes the pick as its base (19785) and records `0x80`. A guest's game skips the pick (hooked, with the
   story manager's switch); the host's pick goes as a `kBase` row (live, and in the cell's state), applied with
   `SetObjectReference` and the 3D dropped and loaded again (Disable/Enable, the form-flags record left as it
   was). Scenario `placed-leveled-items-are-hosts` (failed on the 2b build: 13 books each game's own). Encounter
   zone `0x20000000` (`664DE`) not identified: not in memory since.
3. **Created objects** (`0xFF` ids, each game's own): dropped items, placed objects, loot. A mapping between the
   host's and each guest's ids, made when the host creates one; also what quest fills of created objects need
   (loose ends, "Created objects in quest alias fills"). Dropped items built on it 2026-10-26 (`dropped-items.md`): a guest's
   drop made by the host's game, its own reference adopted as the twin; pickups through the host.
4. **The rest of `unsynced`:** done 2026-10-21. Animation `0x10000000`: the graph row (`kGraph`, the graphs' save
   data, ~130 bytes each) goes with a cell's state only (live changes are the relayed animation events);
   `late-join-gets-ring-pose` (a claw ring turned while the client was away). Activating children `0x4000000`:
   not sent, the host's transient bookkeeping while a reference's activate-parent children run (19428).
5. **Physics objects' rest positions.** Havok moved `0x4`: when the host's body comes to rest (deactivated), its
   position and angle go out through the move row. Each game simulates the flight its own way, then the host's
   resting spot wins. Built 2026-10-21 (`Bridges/PhysicsRests`); since 2026-10-24 the flight itself is the host's too
   (live physics, `Bridges/PhysicsBodies`, below) and the rest is found from Havok's island mark. A guest's own push
   goes to the host as its impulse (physics sync step 5, 2026-10-24).

World sync is then done: every object ends where the host's is. Live physics (the flight itself, ragdolls, a guest's
push as an impulse) is its own plan after phase 5 (`docs/design/host-authority-migration.md`, "After: physics sync").
Then AI state on copies and dialogue's step sync (next section).

### Shared dialogue: sync the step, not the list

Decided 2026-10-19 (`docs/design/shared-dialogue.md`, "Next"; `docs/design/copy-contract.md`, "AI state"): each
game's engine builds its own list from the host's step, against world and AI state that is the host's. Each step
with its scenarios.

Steps 1-3 done 2026-10-22 (overnight slice 5, below), then replaced the same day by the host's list for every menu
("Dialogue: the host's list" below; `shared-dialogue.md`, "Decided 2026-10-22").

1. **Spike:** on the client, set the conversation's line to the host's step and call the engine's build and show
   (35287, 52857); compare with the host's list, list what else 35287 reads.
2. **AI state on copies** (revised 2026-10-20, `copy-contract.md`, "AI state"): the owner sends current package,
   sitting, sleeping, scene, combat state and targets when they change; the copy writes the fields readers read
   and answers `IsInCombat`/`GetCombatState` from it, never with a combat controller of its own. The package
   first (`GetIsCurrentPackage`, 87 menu lines).
3. **Step sync:** the host sends its step at each list it shows (the greeting first, the current one to a
   joiner); peers mark said lines and build their own list. Deletes `OwnLines`, `ShowOptions`, the record of
   options put in the list, `SendOptions`, the host-options message and the `List` filter. The dialogue scenarios
   keep passing; `dialogue-peer-only-option` and `dialogue-picker-says-its-line` check each player's own lines.
4. **Quest topics on peers:** follow from quest sync (missing on a peer's menu until then).
5. **If step sync waits:** list updates event-driven, not checked every update (`DialogueService::ShowOwnLines`
   on the host's list arriving, the peer's engine building its list, a new step).

### Cutscenes: a player's own moment stays theirs (2026-10-23)

Research `docs/research/party-freezes.md`: 81 vanilla scripts take the controls (8 more only in comments), 25 of them
one player's moment (travel, fishing, the lute, traps, beast forms). Built (`docs/design/cutscenes.md`, "Whose
moment"):

- **The rule:** a controls or screen call whose stack a player's action started (GuestScripts' cause: a guest, or
  this game's player 0x14, now tagged on the host too) stays that player's. It is the party's when no player is
  behind it, or when a stage that player's scripts set runs it (`Cause::Story`).
- **Restrictions:** a controls call is shared only if it asks for movement (a hook on `ToggleControls`, 68545).
- **Tests:** scenarios `player-moment-stays-theirs`, `restriction-stays-with-game`,
  `player-stage-cutscene-reaches-party`, each 3 of 3. Everyone gets a cutscene wherever they are (no distance
  gate, decided 2026-10-23). New harness controls: `papyrus_call` `player_action`, and
  `quest_allow_rerun` for a run-once quest.
- **Re-run:** the scripts, quests, magic and dialogue scenarios pass, except the two crashes below and one
  `dialogue-pick-reaches-host` timeout ("[host] FF000854 says the picked line"), which passed 3 of 3 on a rerun, cause
  unknown.
- **Crashes found:**
  - `HookSetOwnerForm` read through an unresolved owner form id while a reference loaded. Fixed:
    `docs/re/crashes/2026-10-23-owner-form-id-on-load.md`.
  - A guest's beast form: its werewolf quest's stage goes to the host as a world call, the host changes the guest's
    copy, and crashed freeing a behaviour graph (`docs/re/crashes/2026-10-23-guest-beast-form-graph-gc.md`). The cause
    is fixed below; the graph crash itself is not investigated.
- **A guest's own quests** (2026-10-23): a quest the guest's own scripts start (the beast form's
  `PlayerWerewolfQuest`, a mod's quest from its MCM) is the guest's: its stages and events stay on its game
  (`PlayerNatives::IsOwnQuest`, a hook on `EnsureQuestStarted` 25003; `docs/design/world-state-sync.md`, "A guest's
  own quests"). `client-beast-form-stays-client` passes again. The first piece of per-player quests
  (`docs/ideas.md`).
- **Two cutscenes at once** (2026-10-23): each game keeps who took which controls (its own scripts, each other game's
  by `NotifyPartyNative::Source`), and a give-back gives back only what no one else holds. A player leaving gives back
  what its cutscene held on the others (`NotifyPlayerLeft`). Scenarios `overlapping-cutscenes-keep-controls`,
  `leaver-cutscene-gives-back`.
- **Lock order:** `client-beast-form-stays-client` printed an engine-only self-deadlock pattern on the host
  (`SkyrimSE.exe+0xcb553c > +0xcb55b7`, write lock on `0x1431ca5d0` asked while holding its read lock). It has none
  of our frames and is not in `lock_order_known.txt`. A second, also engine-only, came up in the tagged run: a lock
  order inversion (`+0x16abbd > +0xe92c16` vs `+0x1b064a > +0x165105`). Both noted, not looked into.
- **Re-run after both:** the new scenarios and `client-beast-form-stays-client` 3 of 3; the scripts, quests, magic,
  dialogue and session tags pass except one `host-rejoins` (Lydia's copy had no animation state yet after the
  rejoin: action, idle and graph states 0). It passed 3 of 3 on a rerun; cause unknown.
- **One script call type** (2026-10-23, cleanup): the cutscene, world-call and player-native paths each had their own
  `{class, function, self, arguments}` struct, frame reader and conversion loops. Now `ScriptCallOf<TForm>`
  (`ScriptCall` on the wire, `LocalCall` in a game; `Structs/ScriptValue.h`), `GlobalCall::ReadCall`/`QueueCall`, and
  `Utils::ToScriptCall`/`ToLocalCall`. `Payloads::PlayerNative` gains the call's self (always None) on the wire. The
  scripts tag passes, and so do the two dialogue native scenarios. `guest-spawns-no-critters` failed once as the
  first scenario after a reload ("[host] its spawner places the dragonflies"), then passed 3 of 3.

### Cutscenes, step 1 (2026-10-23)

The whole party sees a cutscene (`docs/design/cutscenes.md`; research `docs/research/cutscenes.md`): every
script's call on the controls or the screen (fades: image-space modifiers, `FadeOutGame`; camera, shakes, music,
video) runs on every game, each on its own player (`Papyrus/PartyNatives`, `CutsceneService`, `PartyNativeRequest`/`NotifyPartyNative` relayed by the
host). Which calls those are is read from their code (`NativeReach::CallOf`: the native's own code calling the
engine's controls, fade, image-space, camera, shake, music or rumble functions), no list. Decided against a
cutscene gate (2026-10-23): a spell's fade outside one tints everyone, accepted. A guest's own script setting a quest's stage goes to the host (`Quest.SetCurrentStageID`,
`WorldCallRequest`/`NotifyWorldCall`, `HostPlayerNatives::OnWorldCall`): the Elder Scroll's `MQ206.SetStage(20)`.
Scenarios `cutscene-controls-reach-party`, `cutscene-fade-reaches-party`, `guest-cutscene-reaches-host`,
`screen-reaches-party`, `guest-script-stage-reaches-host`; controls without the sending fail. Harness
`papyrus_call` and `screen`. Found on the way: 1.6's `Disable`/`EnablePlayerControls` take nine arguments, and
`GlobalCall` now refuses a call whose arguments did not fit (`Statement::SetSize` returns false). Step 2: each
player's own character in the scene.

### Overnight 2026-10-22: ghost NPCs, AI state on copies, step sync

Order: (0) ghost NPCs, (1) AI state on copies (package, sit/sleep, scene, combat), (2) dialogue's step sync.
Decisions made without the user are marked **Decided**.

- **0. Ghost NPCs: done.** `ActorWhereabouts` on `RequestRemoveCharacter`/`NotifyRemoveCharacter` (host rule
  `HostCharacters::OnRemoveCharacter` passes it on; bridge case "the owner's removal says where its actor went";
  TPTests round trip); the guest's `CharacterService::FollowOwner`; harness `actor_place`. Scenario
  `host-npc-leaves-copy-follows` (send home, stage again, disable, enable) fails before (the client's Vilkas left
  on the stage) and passes after; `release_host_npc` lost its client half. Passed with it: `vilkas-counts-client-hits`,
  `remove-host-npc`, `death-host-npc`, `move-host-npc`, the `cells`, `session` and `spawn` tags but the known
  `party-stacks-on-arrival` and `form-state-round-trips` (loose ends) and `dive-host-player` once (below).
  - **Decided:** the host's deleted NPC is disabled and marked deleted on the guest (as the world state's enabled
    row deletes a reference); its entity goes.
  - **Decided:** a guest's game reference stays flagged a copy after its copy entity is dropped (removal, an
    arrival leaving it behind, a respawn request) for the rest of the session (`HeldCopyComponent`); the flag
    is cleared when the session ends. Before, an arrival cleared it and the guest ran that NPC's AI.
  - **Decided:** the guest skips the move when its reference is in the target cell within 64 units (a host that
    travelled away sends removals of actors that did not move).
  - `release_host_npc` sends the host's NPC home while it is still frozen and thaws it there: thawed and
    unloaded, Vilkas's AI walked him on ~300 units a second (`actor_place` on the host), which a copy does not
    follow (it goes where the actor was at removal).
  - `dive-host-player` failed once in the tag run (the host's copy on the client had a package of type 18,
    `kPackage`, in one sample under water), then 3 of 3. Not traced; the copy is a player's (a temporary
    reference), which this slice does not touch.

- **1. AI state on copies, the package: done.** `AiStateService` (owner sends `AiStateRequest` when it changes, event-driven
  since the cleanup below; `HostAuthority` relays it from the owner only, bridge case "AI state: the owner's reaches everyone else";
  wire bytes pinned); the copy held its owner's form package (`ActorPackage::InitFromPackage` 38959; changed after the
  suite to answered, not run, then back the same day, below). Upstream's package relay is deleted (`NewPackageRequest`/`NotifyNewPackage`
  became `AiStateRequest`/`NotifyAiState`, `AI_SYNC`, `InitPackageEvent`, `HookInitFromPackage`, `SetActorPackage`).
  `AIProcess` now has CommonLib's `ActorPackage currentPackage` (`+0x18`) and `followTarget` (`+0x110`), asserted.
  Harness: status `ai_state` (compared by the desync check), `put_package`, `is_current_package` (the condition
  21529 itself). Scenario `copy-runs-owners-package` (two packages put in place on the host's frozen Vilkas; the
  copy's field and `GetIsCurrentPackage`) fails before (package 0, condition false) and passes after; seen live too:
  the copy followed Vilkas's own evaluation back to his package.
  - **Decided:** the owner sends what conditions read (`Actor::GetCurrentPackage` 37399: for an engine-made package,
    the form package under it); an engine-made package itself (no id shared by the games) leaves the copy on the
    do-nothing one, so `GetAlarmed` (an engine-made alarm package) answers no on a copy (27 menu lines; open).
  - **Decided:** the package target is not written into the copy (no condition in the menus reads it; a copy's
    package aimed at another actor is what the engine's Disable cleanup acts on, the 2026-10-10 deadlock). Since
    the cleanup it is not sent either.
  - `copy-ends-voice-line` asserted the do-nothing package on the copy; it now accepts the owner's package (what
    matters there is that the copy holds one, for `UpdateAI`'s upkeep).
- **2. Sit/sleep, furniture, scene, talking, follow target: partly.** Sit/sleep state (`GetSitting`, `GetSleeping`)
  and talking (`IsTalking`) are the owner's already (replayed actions, the copy's own voice line); `ai_state.talking`
  is compared. Scene: `Actor::GetCurrentScene` (37247) hooked for copies (`Games/Skyrim/CopyAiState.cpp`), compared
  as `ai_state.scene`; **no scenario** (no lever to put a host NPC in a scene without a quest: a stall, left). The
  occupied furniture and follow target travel but are not written (**Decided:** no vanilla dialogue condition reads
  them; writing the furniture handle would hand a copy to the engine's furniture exit code). Greeting
  (`HighProcessData::greetingPlayer`) is not carried (**Decided:** it means "this game's player", which differs per
  game). No host NPC could be seated in QASmoke by the console (`<furniture>.activate <npc>` did nothing), so no
  NPC sit scenario; players' copies are covered by `furniture-*`.
- **3. Combat state on copies: done.** Audit of the 22 `IsInCombat` callers touching `+0x160` written in
  `actor-ai-and-copies.md`: 4 use the controller after a yes unchecked, 3 start or stop combat; the hook answers the
  owner's everywhere else (by return address). `GetCombatState` hooked; `currentCombatTarget` (`+0x104`) written.
  Scenario `copy-in-owners-combat` (Vilkas, AI on, `startcombat player`: the copy's `IsInCombat`, `GetCombatState`
  and combat target = the client's copy of the host's character; then out of combat) fails before and passes after.
  A frozen or restrained actor does not start combat; the scenario thaws Vilkas, who swings at the host's player.
- **4. A guest's player in combat: done.** `HookCountCombatTargetingPlayer` (41261) adds the copies in combat with
  this game's player. **Decided:** counted on the guest from the combatants' AI state, not a count sent by the host
  (the state already says whom each fights). Scenario `guest-player-in-hosts-combat` (Vilkas fights the client's
  character: the client's player is in combat, then out) fails before and passes after. Closes the loose end "A
  guest's player is never in combat"; combat music and wait/save blocking follow from the same flag, not checked.

- **5. Dialogue step sync: done (steps 1 and 3; 2 is slices 1-4), replaced the same day (below).** The archive's client (tag
  `archive/shared-dialogue-client`) redone as step sync on today's tree:
  - Step 1 (spike, IDA): the list build 35287 takes the picked node's option, else `lastSelectedDialogue` (+0x38,
    which it owns); an option whose info has choices (the constructor 35254 sets +0x10 from 25659) makes
    `rootTopicInfo` that info and the list its choices, else the speaker's topics (35276). A game builds a step's list
    by making the said line's option (`BuildOption`) the `lastSelectedDialogue` with no picked node and calling 35287
    (`MenuTopicManager::BuildStep`); the host's step is its `rootTopicInfo` after each list (`DialogueBridge::GetStep`).
    Seen in the games' logs: steps of the speaker's topics and of a line's choices (Andurs, three questions deep).
  - Step 3: `DialogueStepRequest`/`NotifyDialogueStep` (the said line, empty for the speaker's topics) replace
    `DialogueOptionsRequest`/`NotifyDialogueList`; deleted with them: `DialogueOption(s)`, the archive's `OwnLines`,
    `ShowOptions` building options, the added-options record, `SendOptions`, the `List` filter, `GetLine`/`GetChoices`.
    `HostDialogue` sends the step to the games in the conversation and to a joiner and takes a pick only with the
    picker's own line. Ported from the archive: refusals, training, force greets (`DialogueStartFailedRequest`,
    `DialogueTrainingRequest`, `NotifyDialogueRefused`, `NotifyDialogueTraining`, `Everyone`), Speech experience to the
    picker (`PlayerCharacter::GainSkillExperience`, `SetSpeechExperienceFilter`), the copy-speaker guard on
    `TESTopicInfo::Begin` (25616), the dialogue hooks (`Bridges/DialogueBridge.cpp`), the harness (`dialogue`,
    `dialogue_pick`, `skill`, `force_greet`, `info_said`, `trainings`; `item_count` takes `form` alone for the
    player) and the scenarios. `dialogue_tests.cpp` rewritten for steps (27 cases); TPTests round trip.
  - **Decided:** the game side splits by role, per the rules: a shared `DialogueService` (joins, leaves, picks, the
    picker's training and Speech; `Services/Generic`), the host's part `Services/Host/HostConversation` (made by
    `HostService` while hosting) and a guest's `Services/Guest/GuestDialogue` (made by `GuestService`), each plugging
    in as `DialogueService::Role`; not the archive's one service with `IsHost()` branches.
  - **Decided:** a guest with no answer to a line's choices leaves (its menu closes), as the archive did; the host's
    held choices (`HoldChoices`) stay.
  - The 18 two-game dialogue scenarios pass (`--tag dialogue`, first run). The archive's three-player duo did not
    apply on the changed duo code; its half (a `--players` flag `duo` ignored, `client2`, `dialogue-three-players`)
    was removed at review (`docs/ideas.md`, Testing). Steps 4 (quest topics on guests) and 5 (event-driven lists)
    not done; step 5 is moot (step sync has no polled list).
- **A copy's own combat (2026-11-05):** an NPC's attack ran `Actor::StartCombat` (38561) on its copied victim, which then
  ran a whole combat tree. `HookStartCombat` refuses copies (`combat-targets.md`); a dead host's NPCs do turn on the copy
  and chase it. Measured: the half-stop picture comes with the engine's detection losing a target in the dim inn, the real player too;
  a vanilla comparison is left (`bugs.md`).
- **Combat target on copies:** fixed in the reviewed aggro merge (2026-11-02). Native
  `Actor::UpdateCombatSearchState` (37273) clears the target on actors with no native combat. Copies restore their
  cached owner weak handle after that update; their AI/controller stays disabled. The strengthened
  `copy-in-owners-combat` target assertion and both aggro switch directions pass on Steam 1.7.104.
- **Combat state allowlist (crash found by the suite, fixed):** the first hook answered the owner's state to every
  caller but the 9 audited sites; the client crashed in `cast-host-player` (`MagicCaster` 34450 through
  `Actor::EngagingInCombat` 38563 reads the controller at `+0x160`; 23 callers of that wrapper, none in the audit of
  direct callers). **Decided:** a copy answers its owner's combat state only to the condition functions (21576,
  21740) and to plugins (callers outside the game's image); the engine's own callers get the copy's own answer. Papyrus
  `IsInCombat`/`GetCombatState` tail-jump into the vfunc from the VM and get the copy's own (open).
- **A copy's package: held in its slot again (2026-10-22, after review).** Overnight it was changed to "answered, not
  run" (do-nothing in the slot, 37399 hooked) after a lock-order report blamed on a copy running `BGSProcedureSleep`.
  Wrong diagnosis: the stack runs through `HookUpdateAI`'s pass-through (`RealUpdateAI`), so the sleeper was not a
  copy but an NPC of the guest's own, 10 ms after the guest left the host for a late join (same report with the
  do-nothing version; `late-join-gets-ring-pose` passed on rerun, open as a vanilla race on load). **Decided:** the
  copy holds its owner's form package in its running slot, without a target (`Actor::PutOwnerPackage`, 38959;
  `HookCheckForNewPackage` keeps it); the 37399 hook is deleted. Inline readers (Open Animation Replacer) now see the
  owner's package. `copy-runs-owners-package` also checks the slot (new readout `ai.running_package`): fails with the
  do-nothing version (slot FF000EAD), passes now; package, combat, furniture, sneak, late-join and all dialogue
  scenarios pass (dialogue-three-players needs a third game, not run). `ai_state.talking` is no longer compared (a copy's own voice state
  trails its owner's; `vilkas-counts-client-hits` failed on it once).
- `guest-player-in-hosts-combat` failed after the full suite on the same duo (Vilkas's `startcombat` on the client's
  character did nothing, target 0), passes on a fresh duo. Not traced.
- **Full suite at the end (fresh duo, final build except the revert below, 935 s): 107 pass, 3 fail.**
  `flames-client-hits-host-npc` (each game's own fire hazard, the `burning-shows-on-copy` loose end);
  `dialogue-pick-reaches-host` (the client's E press in its menu sent no pick: no log line of one; passed 3 of 3
  on rerun and in the dialogue run); `host-kill-move` (the client's blow 176 ms ahead of the host's; 2 of 2 on
  rerun). The run before the package change: 104 pass, 6 fail (the hazard twice, `party-stacks-on-arrival`, the two
  lock reports and `ai_state.talking`, all handled above). Tried for the pick: skipping the guest's redraw of step 1
  (the speaker's topics its engine built at the open) made two other picks fail; reverted. Not explained: the
  guest's step-1 redraw may reset the SWF as a pick lands.
- **AI state at spawn (found by the suite, fixed):** `late-join-client` failed on a copy made after the rejoin
  holding no package for up to 2 s; the spawn now carries the AI state (`AssignCharacterRequest`/`NotifyCharacterSpawn`
  `AiState`, `AiStateService::ReadState`).
- **AI state cleanup (2026-10-22, the user's review: "this clearly isn't clean code"; "events over polling").**
  - Event-driven: `AiStateEvents` (Games/Skyrim) sinks `TESPackageEvent`, `TESCombatEvent`, `TESDeathEvent` and
    `TESSceneEvent` and queues `AiStateChangeEvent`; the owner reads and sends only on those, if changed (target
    switches and the player's flag since, "Events over polling" below). The 250 ms
    check and the 2 s full resend are gone. `put_package` sends the package event as evaluation does.
  - The wire state is what a copy applies: package, scene, combat state and target (package target, furniture and
    follow target dropped; the status still shows them from `EngineCalls::GetAiReadout`). Wire bytes re-pinned.
  - `GetActorAiFields` is one engine read of an owned actor (no copy branches); the status reads what conditions see
    through the harness instead. The fighting-player count goes through `IActorBridge::SetCopiesFightingPlayer`, not
    from the service into the hooks. Target lookups use `Utils::GetFormIdBySessionId`/`GetSessionIdByFormId`.
  - Found by its run: a death clears the package with no package event (`death-stage-runs-once`: the copy kept
    Valdr's); `TESDeathEvent` added. Open: a combat target switch inside one fight has no event.
  - Runs: BridgeTests 110 cases pass. AI scenarios, `late-join-client`, `host-npc-leaves-copy-follows`,
    `furniture-host-player`, `sneak-client-player` pass; `--tag combat --tag spawn` 27 pass, 2 fail:
    `death-stage-runs-once` (above, fixed, then passed) and `party-fast-travels` (the guest crashed in the engine's
    `QueuedPromoteLargeReferencesTask`, 18902+0x80, null `rcx`, ref 190E5, no frame of ours; new, not traced; passed on
    fresh games). After those, `guest-player-in-hosts-combat` failed on the host's own `startcombat` (target never
    set), the known "fails on a used duo" item above; it passed on fresh games earlier.

- **Dialogue: the host's list (2026-10-22, the user's review: "is there a simpler way?").** Step sync replaced:
  every menu shows the host's engine's list (judged for the host's player, with the real quests and aliases), and a
  pick is an option of that list, which the host's game says as a click. Messages back to `DialogueOptionsRequest`/
  `NotifyDialogueList` (HEAD's, with `Structs/DialogueOption`); `HostDialogue` keeps the step's list, sends it to
  the games in the conversation and to joiners, and takes only a pick in it. Deleted: the guest's list build
  (`MenuTopicManager::BuildStep`, `IDialogueBridge::Line`, `GetStep`, `ShowStep`), `HoldChoices` and the guest
  leaving with no answer, `PickTopic`'s say-any-line, `MarkSaid` on guests. Added: `DialogueBridge::ShowList`,
  `PickOption`, `IsChoices`. BridgeTests: the list cases rewritten, new "a pick is one of the host's options" and
  "a pick before the host's list is not said" (111 cases pass); TPTests round trip for the lists.
  Scenarios: `client_shows_hosts_list` (topics, lines, texts equal, in order) in `dialogue-peer-starts` and each
  step of `dialogue-several-questions-deep`; `dialogue-peer-only-option` → `dialogue-options-are-the-hosts`,
  `dialogue-picker-says-its-line` → `dialogue-pick-says-the-hosts-line`, `dialogue-answer-only-a-peer-has` →
  `dialogue-unanswered-question-ends-it`; new `dialogue-quest-option-from-host` (the guest's own beggar quest
  stopped, its menu still lists the coin).
  - Found by the runs and fixed: (1) the guest's engine builds and shows a list of its own after the host's
    arrived (at the menu's open, after its greeting): the host's list now goes in its place at the engine's show
    (`HookShowList`, the `List` filter, `GuestDialogue::ListToShow`); this was what the said-once, reset-hours and
    quest cases caught. (2) The host's list built again within a step (a topic with random lines, Lydia's): a pick
    falls back to the topic's option (`PickOption`). (3) A menu filled again with the list it already shows takes
    no pick for 750 ms (voice ready 52858): a guest's E press in that window was lost (`dialogue-pick-reaches-host`,
    the intermittent failure of the night before); a refill that changes nothing is skipped. Logs added for a pick
    sent and a pick not said, with why.
  - Not dialogue: after a beggar's coin the host's player has Gift of Charity (`EEF58`), whose effect shader stayed
    on the guest's copy forever (copies held every hit shader; the owner only FX Persist ones): found by
    `dialogue-options-are-the-hosts`' desync check; fixed by the EffectVisuals change ("Events over polling",
    below); the coin scenarios dispel the blessing too.
  - Runs: BridgeTests 111 pass, TPTests pass; `--tag dialogue` 22 of 22, twice in a row on one duo.
  The half-ported three-player duo and `dialogue-three-players` are removed (`docs/ideas.md`, Testing).
  - Bridge cleanup (the user's review: "dialoguebridge is large"): `Redraw`, `Pick(index)` and
    `MenuTopicManager::IndexOf` were dead; one "already shown" check (the bridge's `ShowList`, not also
    `GuestDialogue`), cleared when the engine shows its own list (a stale record could skip a needed refill); the
    host sends a step's list when its engine builds one (the `List` filter as the event) instead of counting lists
    every frame (`GetListsShown`, `m_listsAtPick`, `m_stepSent`).
- **A picked line runs for its picker (2026-10-22, after deciding each player keeps their own items and gold,
  `party-model.md`).** Rule 2 for dialogue: on the host's game a guest's picked line's fragments are queued under
  that guest's cause (`TESTopicInfo::Begin` 25616, the `LineFor` filter, `HostConversation`), so `Game.GetPlayer()`
  there is the picker's character. Natives on the player (`Game.AdvanceSkill`, `IncrementSkill(By)`,
  `IncrementStat`, `ShowTrainingMenu`) in any script run for a guest are skipped on the host and run on that guest's
  game (`GuestScripts`, `PlayerNativeEvent` → `PlayerNativeRequest`/`NotifyPlayerNative`, host-only rule in
  `HostAuthority`, `GuestEngine::RunPlayerNative` through `DispatchStaticCall`). Deleted, the two reroutes they
  replace: the training hook (52942) and `OpenTraining`, the Speech filter on `PlayerCharacter` and
  `GainSkillExperience`, `DialogueExperience*`/`DialogueTraining*`, `HostDialogue::Talker`. Also typed:
  `IFunctionArguments` returns bool (CommonLibSSE-NG; nothing implemented it before), `Stack::returnValue`.
  - Scenarios: the coin is the client's (`dialogue-options-are-the-hosts`), the host does not pay the client's room
    (`dialogue-pick-says-the-hosts-line`), a real persuade (Ungrien, `dialogue-picker-gets-speech-experience`; the
    old one faked it with `advskill`), the training menu over the client's dialogue menu as on the host's game
    (`dialogue-picker-trains`; the reroute closed the dialogue first, vanilla does not, read on the host).
    BridgeTests 110 pass (the two reroute cases out, a player-native relay case in), TPTests round trip.
  - Runs, `--tag dialogue,scripts` on a fresh duo: 28 of 32; `dialogue-options-are-the-hosts` missed once
    ("[host] the client's character carries its gold" after 5 s, the copy behind the client's gold once; it passed
    alone and twice after the same predecessors; the wait now names both counts). `statue-fires-at-client` and
    `ring-turns-for-client` fail on the player's `in_combat` (owner vs copy, AI state from commit 2, no native
    forwarded during them; the events agent's combat work passes them), `shrine-blesses-client` on the lock-order
    checker's known engine pair (our hooks only passing through; the checker fix is the late-join report's).
- **Events over polling (2026-10-22, after the AI state cleanup).** What each engine event or hook covers:
  `docs/re/systems/change-events.md`; what is left and why: `docs/ideas.md`, "Events over polling".
  - **Difficulty: done (2026-10-23).** The engine sets it through `PlayerCharacter::SetDifficulty`
    (40563, after writing `iDifficulty`: the settings menu 53310, its defaults 36622, the saved settings applied
    36598, a profile's); hooked in `PlayerCharacter.cpp`, it raises `DifficultyChangeEvent`. The host's game tells
    the others (`HostAuthority::DifficultyChanged`, `HostSession::OnDifficultyChange`), a guest's puts the host's back
    (`PlayerService::OnDifficultySet`). Both per-frame reads are gone. The fake engine's `ChoosesDifficulty` raises
    it (`session_settings_flows.cpp`); `HostAuthorityTests` "Difficulty: the host's reaches the others".
    Discovery, presence and the equipment snapshot were looked at and stay (`docs/ideas.md`).
  - **Combat target switches (AiStateService): done.** `CombatController::SetTarget` (33235, the only writer of an
    NPC's target, `+0x104`) hooked in `AiStateEvents`; it raises the attacker. Also the player's in-combat flag:
    `PlayerCharacter::UpdateInCombat` (41238) hooked, a flip raises the player (a player has no combat event; the
    desync check caught a host's player left in combat on the host and not on its copy). Scenario
    `guest-player-in-switched-combat` (Vilkas fights the host's player, then the client's character once the host's
    player steps 1500 units away; the client's player must be in combat): fails before (its Vilkas stays on the
    host's character), with the hook file left out too, and passes after (3 of 3, and in the tag runs).
    **Decided:** the combat scenarios' cleanups freeze Vilkas before `stopcombat`: thawed, he takes up the fight
    with the host's player again within 2 s (vanilla, seen without the hooks too), and `guest-player-in-hosts-combat`
    then left the host's player in combat.
  - **EffectVisualsService: done.** The owner sends its actors' effects when its engine starts or stops their hit
    visuals (hooks on 34094, ours `ActiveEffect::StartHitEffects`, and `ActiveEffect::StopHitEffects` 34050, in
    `EffectEvents`); `TESActiveEffectApplyRemove` is sent for scripted effects only (Flames' fire sent none). The
    250 ms check, the 2 s full resend and the copy's 5 s "owner went quiet" stop are gone. `AssignCharacterRequest`
    and `NotifyCharacterSpawn` carry `EffectIds` (TPTests round trip, bridge case "a player's own gets an id..."
    checks them; the spawn messages have no pinned bytes). The copy keeps its owner's list by session id (a respawn
    keeps it), replays it when its 3D loads (`TESObjectLoadedEvent`, `ReferenceEvents`) and stops it on the
    character's removal or a disconnect. Found by the main session (Gift of Charity on a copy): copies held every
    effect's visuals until it ended, the owner only those with FX Persist; **decided:** only FX Persist effects are
    shared (`GetHitVisuals`, typed `EffectSetting` fields instead of raw offsets). Scenario
    `copy-made-later-shows-effects` (Vilkas casts Oakflesh while the client is away; the copy made on its return
    shows it, then a dispel ends it on both): passes, fails with the spawn's list left empty.
    `burning-shows-on-copy`'s body passes (its world-differs failure is the hazard loose end).
    Then (main session, merging this): one-shot visuals no longer vanish from copies. `StartHitEffects` raises the
    effect when it does not persist, the owner sends `EffectFlashRequest` (relayed from the owner,
    `NotifyEffectFlash`; bridge case, wire pin) and the copy plays it with a lifetime of 0, which the engine ends
    after the shader's own pass as it does its own (`change-events.md`). Scenario `copy-flashes-one-shot-effects`
    (Gift of Charity on Vilkas: the flash shows and ends by itself on both, the blessing still running), read
    through the harness's `mod.visuals_shown`: a one-shot shader is finished from its start, which `visuals` leaves
    out, so the first version (Arkay's blessing, whose `0x92DEC` is its enchant shader, not a hit shader) never saw
    the host's flash. 3 of 3.
  - **Copies sweeping across a teleport (fixed, the merge's runs).** A copy whose owner went to another interior or
    worldspace was moved there at its old cell's coordinates and interpolated from the old points to the new ones,
    across the new cell: through QASmoke's water on the way home (z 1060 up to the floor at 6976), so the engine
    set the copy swimming (`ActorState1` bit 10, `Character::sub_37946`), and a copy never leaves it on land
    (leaving needs its controller's `kSupport` or the in-air state). A swimming guest's character is never taken as
    Vilkas's target: `guest-player-in-switched-combat` failed after `plate-counts-client` (Kolbjorn and back), not
    alone (the merge was not the cause), and `dive-host-player` failed on `SwimStart` vs `swimStop` after the
    return home (the copy's own swim start; the owner's `swimStop` replay was refused). Now the owner's movement in
    another interior or worldspace drops the copy's earlier points and places it at the new position, as does an
    arrival's spawn (`CharacterService::OnReferencesMoveRequest`, `RefreshCopy`). plate then switched-combat 2 of 2,
    `dive-host-player` 5 of 5, `dive-client-player`, `party-changes-cells` pass. A teleport inside one cell (`setpos`)
    still sweeps.
  - **PlayerService: done.** Beast form on `TESSwitchRaceCompleteEvent` (struct asserted), the level on a hook on
    `TESActorBaseData::SetLevel` (14385), the one write: the level-up sends `LevelIncrease` before writing, the
    console's `setlevel` (the baseline's reset) sends nothing. Both polls gone (`PlayerEvents`). `party-shows-levels`
    (2 of 2) and `client-beast-form-stays-client` pass.
  - **CharacterService factions and experience: done.** Factions on hooks on their five writers (`FactionEvents`:
    `AddToFaction` 37686, `RemoveFromFaction` 37688, the rank setter 37677, `SetCrimeFaction` 37381, Papyrus
    `RemoveFromAllFactions` 54735); the owner sends an actor's factions when they differ from the last sent. Found:
    a copy has no `CacheComponent`, so `OnFactionsChanges` never applied a change to any copy (only spawns carried
    factions); fixed. Experience goes out on each skill use (the hook was already there, the 1 s timer only
    batched). New harness `faction_rank` (the condition GetFactionRank) and scenario `copy-holds-owners-factions`
    (add, rank, remove on the host's Vilkas): passes (failed on the copy-side bug first). `shared-xp`,
    `vilkas-counts-client-hits`, `late-join-client` pass.
  - **InventoryService: not done (stalled).** The 3 s equipment snapshot repairs a copy that drifted in its own game
    (`repair-equipment-drift`), which no owner event sees; the weapon state has no single writer; the naked NPC
    check's cause is unknown. Tried and reverted: snapshots on the owner's `TESEquipEvent`, the naked check on 3D
    loads. Reasons in ideas.md.
  - **ActorValueService: plan only** (ideas.md); **HostWorldState: not done** (alias and location fills and globals
    have no event); **DialogueService:** skipped (the shared dialogue rework in the main checkout).
  - Runs (final build, one duo): BridgeTests 110 cases, TPTests pass. `--tag ai,combat,magic,spawn,session`: 73 of
    76 pass. `join-refused-shows-why` needed `coop_peer` built in the worktree (then passed),
    `flames-client-hits-host-npc` is the hazard loose end, `late-join-gets-ring-pose` hit the known lock-order report
    with only `HookUpdateAI`/`HookMainLoop` of ours on the stacks (a guest NPC's `BGSProcedureSleep` during the late
    join's cell load). Earlier, `sneak-host-player` failed twice on a Fader Menu stuck after a first lever for the new
    combat scenario (lifting the host's player 2000 units); it passed again once the lever changed.

- **Rule 2's gaps (2026-10-22, after the picked line).** Three generic rules, no script named:
  - *A player in the frame:* in a stack run for a guest, a native's `self` or argument holding the player (a
    `PlayerRef` property, a variable stored at load) is that guest's character for the call (`GuestScripts`,
    `PlayerInFrame`). Scenario `relief-takes-client-relics` (Volunruud's relief door, `player` stored at `onLoad`);
    control with the swap off: fails at "[client] the player gave up the axe".
  - *Natives on the player, read from the code:* no list. A global native whose code (three calls deep) addresses
    this game's player, its stats or its screen runs on the guest's game (`Papyrus/NativeReach`, Zydis at run time,
    once per native); IDA's reading of the same rule (`scripts/re/papyrus_natives.py`, `docs/re/papyrus_natives.tsv`:
    65 of 143 global natives) is checked by `natives-reach-matches-ida`. It also moves `FastTravel`, prison, beast
    form, words and the screen natives to the guest's game (`papyrus-guest-scripts.md`, "Natives on the player: read
    from the code"). Any form goes as an argument or result (an actor by session id, the rest by `GameId`,
    `Variable::SetForm` packing it as the engine's per-type packers do), and None: `words-reach-party` teaches the
    QA shout button's twelve words to the client (and, since feature 11, the party).
    A value comes back from the guest's game: the host's stack waits as on a latent native (104851,
    `kInProgress`) until `PlayerNativeResultRequest` → `HostPlayerNatives` → `NotifyPlayerNativeResult` →
    `ReturnFromLatent` (104807); a guest leaving answers with the default. `IFunction::GetReturnType` returns a
    `TypeInfo` through a hidden pointer (it was declared as a reference: the first call crashed the host).
  - *The copy's gold after training:* the Training Menu pays through `InventoryChanges::RemoveItem` (16059), under
    the `TESObjectREFR::RemoveItem` hook, so the host's copy of the guest kept every lesson's gold
    (`dialogue-options-are-the-hosts` failed after `dialogue-picker-trains`). Hooked; skipped inside the reference's
    own removal (`ScopedItemRemovalOverride`), so each removal goes out once.
  - *What the copy stands in for:* a player's copy already carries its owner's actor values, skills included
    (`ActorValueService`); `dialogue-picker-gets-speech-experience` now checks the copy's Speech and the persuade's
    `QueryStat` answering from the client's game. Ownership is a decision for the user: the engine refuses a copy's
    (created) base as an owner (11609), so a guest renting a room pays and owns nothing (`party-model.md`, "Items and
    gold": options A, the party's, recommended, and B, that guest's).
  - Tests: BridgeTests 112 pass (a value answered once and only to the host's game; a guest leaving answers with
    none), TPTests 5 cases pass (round trip of `PlayerNativeResultRequest` / `NotifyPlayerNativeResult`).
    `--tag dialogue,scripts`: 31 of 35; `statue-fires-at-client` and `ring-turns-for-client` on the player's
    `in_combat` (known, fixed elsewhere), `dive-host-player` and `relief-takes-client-relics` on one lock-order pair
    on the client, engine against engine (`GarbageCollector` demoting a reference, 11787, against a sleeping NPC's
    idle conditions, 11778; ours only `HookMainLoop` and `HookUpdateAI` passing through). The relief, the persuade
    and the training passed again on the final build, the relief twice.

### Phase 5: the server is gone (2026-10-21)

Every message a game sends had a host rule; the two without one were dead (upstream's `RequestQuestUpdate`,
never sent, and `AssignObjectsRequest`). What the host still took from `GameServer` moved:

- **`HostNetwork`** (`Services/Host/`): the enet listener (`Net::Server`), each message to the front door
  (`HostAuthority::Handle`), and in-process routes: the host's own game (the transport's local connection) and, in
  `BridgeTests`, every test game. A route carries plain messages (no packet type byte). Ports from 10578 upward.
- **Character ids**: `HostCharacters` counts them from 1, never reused in a session; 0 is no character (the server
  handed out entity ids from 0, and `AnimationSystem` already took 0 as none).
- **Player records**: only `HostPlayers` (the server's shadow records, quest logs and `DiscordId` went).
- `HostService` starts and stops hosting at once (the server stopped on its next update).

Deleted with it: `Code/server` (Lua, admin, console commands, recording), `admin`, `admin_protocol`, the server-only
components (console, es_loader, crash_handler, resources), `native_client`, the replay and `fake_player` tests (and
`coop_test.py net` and `game`, which ran them), upstream's quest relay (`QuestService`, `QuestLog`, `CharacterData`),
the never-implemented `IEventBridge`, upstream's CEF UI and launcher (`skyrim_ui`, `tp_process`, `libraries/ui*`,
`one_ui`, `immersive_launcher`, `immersive_elf`, the overlay and Discord stubs), the MinGW build (its branches,
`devbuild` mode, `SKYRIM_STRUCT_ASSERT`, `MinGWCompat.h`, `link_stubs.cpp`), the Docker files and the old relay-era
scripts (`tests/`, `deploy_and_test.sh`, `fix_ptrace.sh`, `scripts/smoke_test.sh`). The Linux build is clang 18 now
(sol2 had kept it on GCC), and `scripts/compile_commands.sh` gives clangd both builds.

Then (2026-10-21): the upstream root files went too (CI workflows, release config, msvc-wine and Docker scripts,
Windows batch files, the old planning docs, `Tools/`), and the glossary's renames were made: `ServerId` →
`SessionId` (status key `session_id`), `ClientMessage`/`ServerMessage` → `RequestMessage`/`NotifyMessage` (with
their opcodes and factories), and the host → game messages named as requests became notifies
(`NotifyCharacterSpawn`, `NotifyReferencesMove`, `NotifyTimeSettings`, `NotifyWorldState`; the guest's move is
`ReferencesMoveRequest`). Merging the actors' session ids with created objects' ids: `docs/ideas.md`.

Dead code after that (2026-10-21): `Code/base`'s unused parts (SimpleIni, `fast_queue`, `Check.h`, `Testing.h`, the
task dialog: the Address Library error is a message box now), `SkyrimBridge`'s never-implemented stubs (14, none
called), upstream's F4 "reveal other players" (needed `SkyrimTogether.esp`), `BGSSaveLoadManager`, `MenuControls`,
`Overrides.cpp` (the counters are inline). The DLL builds with no unused-code warnings; `offsetof` on engine structs
and clang-cl's PCH include rule are silenced as expected. The one stub something called, `SetActorRotation`, is
implemented: a copy now spawns facing its owner's way (it kept its created heading until the owner's next move).

Leaving `TiltedPhoques` (2026-10-21, `docs/ideas.md`, "Modern C++ over `TiltedPhoques`"), all four steps:
1. No scoped allocators (the transport's send, the enet clock sync, a debug view): every allocation is rpmalloc
   through global `new`.
2. Containers are `std`: `std::vector`, `std::string`, `std::unordered_map`/`set`, `std::unique_ptr`; the hopscotch
   package is gone. Maps that go on the wire are ordered `std::map` (bytes in key order, as `wire_tests` pins them).
   A hot reload refuses this change (actor extension layout): restart the games.
3. `TP_THIS_FUNCTION`/`TP_MAKE_THISCALL`/`ThisCall` are plain C++: `using T = R(C*, A...)`, `R Hook(C* apThis, ...)`,
   `f(this, ...)` (an engine address wrapper converts to its function pointer). One call bound an rvalue to the
   engine's `uint32_t*&` through `ThisCall`'s copy; it names the copy now (`TESObjectREFR::MoveTo`).
4. The rest moved into our tree and `Code/TiltedCore` and `Code/libraries` are gone: the wire buffer is
   `Code/encoding/Wire` (`Wire::Buffer`, `Wire::Serialization`), the enet transport `Code/network` (`Net::Server`,
   `Net::Client`), the MinHook wrapper, patch helpers and D3D11/input hooks `Code/client/Hooks` (`Hooks::`, `HOOK`,
   `Hooks::Initializer`), `TaskQueue` the client's own, the CRC64 of animation descriptors `Games/Crc64`, and global
   `new`/`delete` call rpmalloc directly (`Code/base/allocator`). The `App`/`Entry` framework, its WinMain hooks and
   the SKSE loader went (`Plugin` is a plain class: `InstallHooks`, `Start`, `Update`). Found on the way:
   a bridge test read a message after freeing the outbox that held it (`dialogue_tests.cpp`, `Talking::Open`);
   the custom allocator had hidden it. `scripts/re/build_re_data.py` finds hooks by `HOOK` now (124 hooks).
   Checked on a fresh duo: 24 scenarios across sessions, combat, magic, doors, travel, quests and the world pass
   (12 first failed behind a stuck `LoadWaitSpinner`, loose ends), and a hot reload.

Dead code after `TiltedPhoques` (2026-10-22): four notifications nothing ever sent, not even upstream's
server (`NotifySpawnData`, `NotifyPlayerRespawn`, `NotifyRelinquishControl`, `NotifyWorldState`), and their
handlers; `NotifySetTimeResult` (a guest never read it; the host ignores an hour that does not exist);
unpublished or unheard events (`ActorSpawnedEvent`, `ConnectionErrorEvent`, `PreUpdateEvent`); 20 headers
nothing included (`Differential.h`, engine structs never used); `WindowsHook` (never installed); upstream's
release-branch flag `IS_MASTER` (`BranchInfo.h`); the CRT heap swap and the EngineFixes rehook in
`Games/Memory.cpp`, both only ever called by upstream's launcher. Renamed: `TiltedOnlineApp` is `Plugin`,
`TiltedOnlinePCH.h` is `ClientPch.h`, `TP_ASSERT` is `Utils::Assert` (a function, `std::source_location`),
and the components' include guard is gone. 11 scenarios pass on a fresh duo (sessions, time, combat,
magic, chat).

Includes (2026-10-22): `ClientPch.h`, forced into every client file, held our own often edited headers
(`Utils.h` → `World.h` → every service, `Components.h`), so editing any of them rebuilt all 201 client files.
It now holds third-party, standard and rarely edited headers of ours (`RTTI.h`, `Primitives.h`, `VersionDb.h`,
`Wire`, `Hooks`); files include the rest themselves. `World.h` forward-declares the services it hands out,
`Utils.h` no longer includes `World.h` (its session-id lookups are in `Utils.cpp`), `POINTER_SKYRIMSE` lives in
`VersionDb.h`. Builds (DLL, `-j8`, duo running), before → after: full 33 s → 26 s, one `.cpp` 2.8 s → 2.5 s,
a component header 20.5 s → 7.2 s, a service header 21.8 s → 4.7 s, `Actor.h` 22 s → 6.7 s. Also deleted:
upstream's `SAVE_STUFF` blocks and `Save_Reversed` (never compiled).

`Wire` with spans (2026-10-22): `Buffer` owns a `std::vector`; `Buffer::Reader` reads a `std::span<const uint8_t>`
(received packets are read in place, no `const_cast`), `Buffer::Writer` grows a `Buffer` or writes into a fixed
span and fails past its end (`ViewBuffer`, deleted, grew by freeing memory it did not own). Byte reads and writes
and `vp::ScopedContext::WriteBuffer` take spans; the unused `vp::Put` (no return) and `vp::Nop` are gone. The
wire bytes are unchanged (`wire_tests`); `encoding.cpp` checks growth and both ends.

`std::format` (2026-10-22): `fmt::` is gone from our code and spdlog formats with `std::format` (package config
`std_format`); the Linux build takes libstdc++ 13 (GCC 13 headers from the `ubuntu-toolchain-r/test` PPA), the
first with `<format>`. Format strings are checked at compile time again (`FMT_CONSTEVAL=` had switched fmt's
checks off), which found seven calls fmt formatted wrong or by accident: `MagicService` logged every `GameId`
(spell, effect) as `1` through its `operator bool`, now `LogFormat()`; enums and `volatile` lock words are
converted, `BSFixedString` passes its text. `fmt::join` is `views::join_with | ranges::to`.

## Sync by system

Each verified system has a two-game scenario (`scripts/coop_test.py scenario <name>`).

| System | Status | Scenarios |
|---|---|---|
| Session, join, leave, kick, host rejoin | works | `session-roster`, `late-join-client`, `host-rejoins`, `host-kicks-client` |
| Movement (players, host NPCs) | works | `steady-state`, `move-host-npc` |
| Cell changes (`coc`, doors, fast travel) | works | `teleport-keeps-copies`, `party-changes-cells`, `party-uses-door`, `party-fast-travels` |
| Equipment and hands | works | `equip-*`, `repair-equipment-drift`, `hands-survive-reload-*` |
| Draw / sheathe | works | `draw-*`, `quick-sheathe-*` |
| Sneak, swimming, diving | works | `sneak-*`, `dive-*` |
| Casting | works: copies replay in step with their graph and cast for free | `cast-*`, `input-mouse-cast`, `firebolt-*`, `flames-*` |
| Health | works | `health-*` |
| NPC death and removal | works: a copy dies with its owner's death action | `remove-host-npc`, `death-host-npc` |
| A player going down (`docs/design/death.md`) | works: a real death in a session (`OnDeath` and `TESDeathEvent` fire, the reload held), copies die by their owner's death action, spectating once dead, revived beside the party (abilities, hands, graph and camera back; 2026-10-26); after a wipe the dead host loads a save it chooses (nothing loads by itself, 2026-11-16) and each guest stands where it stood in it; each player's ragdoll streams from its own game to the others (2026-10-26) | `death-spectate-host`, `death-spectate-guest`, `death-wipe-host-loads`, GameTests `[death]` |
| Melee and spells on host NPCs | works, with kill credit | `melee-client-hits-host-npc`, `firebolt-*`, `flames-*`, `client-*-kill-credit` |
| Friendly fire (`docs/design/friendly-fire.md`) | built 2026-10-26: a player's sword or spell on another player is applied by the victim's game; a kill is a real death named in every chat; not a crime; kill moves on players work (a copy's ragdoll steered, and kept on it by the engine as its owner; a copy getting up on its own timing: open) | `friendly-fire-*`, `spectator-watches-kill-move`, `kill-move-kills-guest` (fails on the open jitter), GameTests `[friendly-fire]` |
| Kill moves | works, both ways, with kill credit; players moving and crossed kill moves fixed; crossed, the first swing wins; a copy holds its pose until its owner's death (2026-11-05, `docs/re/systems/kill-moves.md`) | `host-kill-move`, `client-kill-move`, `kill-move-runs-full-length`, `kill-moves-cross` |
| Hostility | works | `client-angers-host-ally` |
| Effect visuals (burning, hit shaders) | works | `burning-shows-on-copy` |
| Cutscenes (step 1: scripts' controls, fades, camera, shakes, music for the party) | works, from any game; a player's own moment stays theirs; two at once keep their controls; each player's own character in the scene not yet (step 2) | `cutscene-controls-reach-party`, `cutscene-fade-reaches-party`, `guest-cutscene-reaches-host`, `screen-reaches-party`, `player-moment-stays-theirs`, `restriction-stays-with-game`, `player-stage-cutscene-reaches-party`, `overlapping-cutscenes-keep-controls`, `leaver-cutscene-gives-back` |
| Beast form (a guest's werewolf change) | works: the guest's game runs it (owner-applied effect), its copies respawn for each race from the owner's state; upstream's host-player bug does not occur (2026-10-20) | `client-beast-form-stays-client` |
| A player's looks changing in a session (race menu, vampirism, any race switch) | works: its copies respawn from its new base data, tints (the shown ones: a vampire's overlay) and weight (2026-10-24); a copy's head built again (a new area) is painted again (2026-11-08, `docs/re/systems/copy-face-tints.md`) | `guest-changes-looks-in-session`, GameTests "A player's looks change", "A player's copy keeps its painted face" |
| Furniture (live, while the copy is loaded) | works | `furniture-*` |
| Voice lines, subtitles, lip sync | works | `copy-ends-voice-line` |
| Doors, locks | works | `door-opens-for-both`, `lock-reaches-others`, `load-door-not-replayed` |
| Containers' items (changes, as counts per base item) | works both ways; the host's contents (its leveled roll) when a guest's cell loads (slice 2, 2026-10-21); an item's extra data (enchantment, tempering, poison) does not travel | `chest-take-reaches-others`, `guest-opens-hosts-roll` |
| Time, weather | works | `host-time-reaches-client`, `guest-sets-time`, `host-wait-reaches-guest`, `shared-wait-host`/`-guest`, `shared-sleep-host`/`-guest`, `shared-wait-cancel`, `late-join-gets-calendar`, `party-fast-travels` (days passed), `weather-follows-host` |
| Party panel (RmlUi) | works alone; fails after other scenarios | `party-panel` |
| Player levels (party panel) | works | `party-shows-levels` |
| Real input (keyboard, mouse) | works | `input-keyboard-draw`, `input-mouse-cast` |
| Mounts | covered; ownership returns after dismount/travel/leave | `guest-rides-host-horse`, `host-rides-horse`, `guest-rides-across-border`, `mounted-guest-leaves`, `party-travel-mounted-guest` |
| AI package, dialogue, factions | relayed; individual coverage documented | `docs/design/copy-contract.md`, `docs/design/shared-dialogue.md` |
| Physics: objects and ragdolls | **2026-11-15: held to a vanilla baseline** (four collision trials, 40 vanilla rounds: 20 with no session, 20 with the plugin's physics hooks off; `physics-sync.md`, "One simulator per pile"): one game simulates each object (the mover's, what its objects knock too; copies push nothing; no relayed push; the host's own placement wins), others steer theirs onto it. A guest's push no longer jumps to the host's result (300 units to under 2 on the ladle line); 2026-11-16: the last two full runs pass 6 and 7 of the 8 `*-like-vanilla` trials (40 vanilla rounds each; in the second every trial that ran matched vanilla, the eighth timed out in staging): pushes and carries match from both walkers, the tavern pile in most runs; its misses are rests a few units apart and a snap now and then (`bugs.md`, "Physics against vanilla"). A rest gets its contacts before it sleeps, glides on, sleeps a still pile as one (Havok sleeps islands, not bodies), a rest being put is not the host's own move (the hopping ladle), the host's own placement drops a guest's rest, and moved bodies are asked about together. Before that: the host simulates, guests show it live (keyframed by its frames, sent when the guess goes wrong) and rest at the host's spots; a dead NPC's copy lies as the host's bone by bone. A guest's push goes to the host as its character's impulse, and its weapon's hit as the swing's (2026-11-13: the hit had snapped back; one at a body the host's frames move does nothing yet; a still copy no longer keeps the clutter beside it awake), its casts through its copy's, what it grabs follows its hands on the host and falls from where it let go; the guest shows its own until the host's rest, and gets the host's pose back for what its own physics moved alone. A guest gets frames only of bodies within 4096 units of its copy, rests of all (2026-10-25). A guest's playback is placed at its pose each frame and blends into frames it has early: no flicker, 0.1 units typical off the host's (2026-10-25, `FlightTrace` in the fireball scenarios). Since 2026-10-26 every game streams its own bodies (a guest's player's ragdoll, what it holds), and a living actor's copy is knocked down with it and lies as its ragdoll does. Not yet: a refused second grab, a dead body's pose at a cell load (`docs/design/physics-sync.md`) | `physics-push-reaches-guest`, `ragdoll-lies-as-host`, `knockdown-lies-as-host`, `guest-knockdown-lies-on-host`, `guest-bump-moves-host`, `guest-swing-moves-host`, `guest-cast-moves-host`, `guest-throw-lands-on-host` |
| Crime / party bounty | host-authoritative | shared totals, guest alarms and fines from the party's purse; native coverage and jail/trespass-AI limits: `docs/design/shared-party-bounty.md` |
| Dropped items | one in the host's world with their extra data; pickups granted by the host (feature 6, 2026-10-26); a dead player keeps what it held, out of reach, and holds it again on respawn (2026-11-06, `re/systems/death-drops.md`) | `docs/design/dropped-items.md` |
| Followers | on dev: shared vanilla slots, member anchors, fallback and picker inventory; native coverage/limits in design | `docs/design/followers.md`, `follower-*` |
| World state (quests, globals, containers, mods' state) | not synced; what drifts and how often is measured (world-state sync steps 1-2: `save-diff`, `change-rate`, `docs/research/world-state-drift.md`); a form's save data round-trips in a running game (step 3, 2026-10-20: `Games/FormState.cpp`, `form-state-round-trips`; the copies' appearance uses it); quest walks 2026-10-20: `coop_test.py quest-walk` drives real quests (main, side, radiant) from a late save (`scripts/fetch_late_saves.py`) and shows the client never follows (`docs/research/world-state-drift.md`, "Quest walks"); step 4 built: globals and quests from the host (`HostWorldState`, `NotifyGlobals`, `NotifyQuests`; a guest applies with the engine's own calls, stage fragments skipped; scenarios `global-reaches-guest`, `late-join-gets-globals`, `quest-stage-reaches-guest`, `quest-completes-for-guest`, `quest-roles-match-host`, `late-join-gets-quests`), the main-quest walk matches 14 of 14; loose ends below. Step 5 slice 1 built 2026-10-21: a guest's world scripts get no activation or trigger events, the host's run them for its copy, and their doors, animations and spells reach the guest from the host (`Papyrus/WorldEvents.cpp`, `HostWorldState`'s `NotifyObjectChanges`, `MagicTarget.cpp`; `docs/re/systems/guest-world-events.md`; scenarios `sword-box-asks-client`, `statue-fires-at-client`, `ring-turns-for-client`, `shrine-blesses-client`). Step 6's enabled state built 2026-10-21: the host's references enabled or disabled reach the guests (`NotifyObjectChanges`; `sword-box-asks-client`, harness `ref_enabled`). Locks the same day: the host's lock changes go out as object changes, a guest's are proposals the host's game takes (`HostAuthority::OnLockChange`; `lock-reaches-others`); the old lock relay is deleted. Both read on the host from the engine's change funnel (`Forms/FormChanges`, `HostEngine::TakeObjectChanges`; `docs/design/world-state-sync.md`, "As built"), built 2026-10-21 (scenarios `lock-reaches-others`, `guest-loads-propose-no-locks`, `sword-box-asks-client` pass); the `Enable`/`Disable` hooks are deleted. Containers slice 1 the same day: a container's item changes travel as its count now (`kItems` object changes from the host; a guest's own a `ContainerChangeRequest` the host's game takes), seen through the engine's `TESContainerChangedEvent` (`ContainerEvents`); `chest-take-reaches-others`; the unused `RequestObjectInventoryChanges`/`NotifyObjectInventoryChanges` deleted. World desync check for references (step 7) 2026-10-21: harness `world_state` (with each reference's change record, pose, physics, graph, effects), `coop_test.py world-diff`, scenario `world-changes-reach-guest` (also each change's object events). Reference sync reworked the same day from what it showed: rows for enabled, lock, open state, move, scale and owner (the engine's calls: swing, events, 3D, collision), the funnel also watching `RemoveChange` and repeat moves; every kind but created references matches in every side effect, items' container event included. The save-data fallback is built but off until mods (with it on, it hid the missing container event); flags with no row are logged once, `coop_test.py unsynced` (now: havok moved, activating children, animation). Open: created references, those rows, whether the guest's scripts should get object events at all, references the guest has not loaded (changes dropped). Rewritten 2026-10-20: save transfer at join, plain messages while playing (step 4: globals and quests), save data per form only for mods later; AI state travels on its own | `docs/design/world-state-sync.md`, `docs/re/systems/form-state.md` |
| A client's actions counting on the host ("who is the player") | spikes 1-5 and a script scan done; decided (`docs/design/party-model.md`): every party member is the player in the host's scripts, "the player" is the member whose action started the script, position questions without a cause ask about the party, story events judged for their member, messages to everyone. Rules 1-2 built (`Games/Skyrim/Papyrus/GuestScripts.cpp`; rule 2 never ran until 2026-10-20: `GetPlayer`'s name reads back from the engine's string pool as `getplayer`; engine names are now compared case-insensitively everywhere, `Games/EngineNames.h`, `docs/re/systems/engine-names.md`; scenarios `plate-counts-client`, `sword-box-asks-client`, `statue-fires-at-client`, `ring-turns-for-client`, `keyhole-asks-client`, `shrine-blesses-client`, `vilkas-counts-client-hits`); upstream's patched vanilla scripts and `SkyrimTogetherUtils` natives removed; rules 3-5 not built. The client's world scripts stop reacting to activations and triggers (world-state sync step 5, slice 1, 2026-10-21); other events still run on both games | `docs/research/player-events.md`, `scripts/spikes/` |

## Copies (how other players' and the host's actors appear)

Copies are puppets: they run no engine AI and replay what the owner did (`docs/design/copy-contract.md`).

- Done: no packages (`8b464c19`), hands owned by the player (`704a82d6`), hit feedback and effect
  visuals (`1dd5628c`).
- Done 2026-10-12 (`719c2409`..`46bbad21`): the AI update skipped for copies with the body upkeep kept,
  the do-nothing package, the owner's graph state loaded at spawn, sneak/dive/subtitle scenarios.
- Done 2026-10-13 (`f3716725`..`e145b097`, `docs/re/systems/actor-ai-and-copies.md`):
  - Replay in step with the copy's graph update (`HookUpdateAnimationGraphs`, 20123): fixed the magic
    chain (refused `MRh_SpellAimedStart`, the copy left on `Magic_Equip`).
  - Copies cast for free (`kSkipCheckCast` in `CheckCast` and the caster's `Update`): no own magicka spent.
  - Health, magicka and stamina from the owner within 100 ms.
  - A copy dies with its owner's death action (`KillWithDeathAction`), never from health alone.
  - An owner's actions before its id arrives are kept (NPCs falling after `placeatme`).
- Next audits: combat, detection, magic/magicka; then one copy update in place of scattered
  `IsCopy()` checks.
- Built 2026-10-22: copies carry their owner's AI state (package, scene, combat state and target; sitting,
  sleeping and talking by replay), simulated only by the owner, read 1:1 by anything that asks
  (`copy-contract.md`, "AI state"; `actor-ai-and-copies.md`, "Combat state and its readers"). Open: alarmed
  (engine-made packages), the scene's own state (`IsScenePlaying`, quest sync), furniture and follow target
  written. Graph events from outside the actions wait for mods.
- **Built 2026-11-02: copies step through the engine.**
  - What changed: `Actor::Move` (37507) runs toward the owner's place each frame instead of `ForcePosition`, which
    now happens only past 64 units. A stepped copy takes the engine's position write-back.
  - Why: a held bucket pressed against a copy sank into it (2026-11-02 playtest).
  - Result: the bucket now stops 18 to 21 units from her axis, as against an owner (`guest-holds-into-npc`).
  - Details: `docs/re/systems/actor-movement.md`, "Copies step".
  - Tests: GameTests "A guest's player presses a held plate against a host NPC's copy"; ClientTests
    `[copy-step]`.
  - Found while checking it, failing the same with the old placement:
    - `actors-walk-after-arrival`: the host's own Riverwood NPCs jump 125 to 573 units in 2 s;
    - `knockdown-lies-as-host`: the copy turns away for about 0.3 s at its get-up, in 1 of 2 runs.
  - Found the same day (`guest-drops-item-stays`, 2 of 5 runs): the host's copy of a sword the guest dropped
    rests 44 to 67 units from the guest's, and nothing corrects it.
  - Fixed the same day: the host's held bucket fell on the guest and was put back, over and over. The user saw
    it while watching `host-holds-into-npc`.
    - Cause: pressed still against Lydia, Havok slept the host's held bucket, so `PhysicsBodies` sent it as a rest.
      The guest's rest is free and asleep, in mid-air; it fell, the guest asked for the host's pose (1 s later), got
      the same rest back, and fell again.
    - Fix: a body this game's player holds is never at rest (`IBodyReads::PlayerHolds`).
    - Measured: 26 falls in 30 s before, 0 after.
    - Tests: the scenario now checks the other game's bucket; GameTests "The host's player holds a bucket still";
      ClientTests "A body this game's player holds still is not at rest".
    - `host-sets-down-item` and `host-drops-item-stays` pass 2 of 2.

## Test suite

- **Join flow, stages 4 to 6 (2026-10-24, worktree `join-flow`).** Every join gets the host's world by
  save transfer: the host's game saves on its main loop (`JoinBridge`, `MainLoopTasks`: a save inside our update,
  which runs in the VM's update job, hung the host), snappy-packed chunks (`HostSaveTransfer`), the guest writes
  `SkyrimCoopJoin` and loads it, puts its character on the save's player, then joins. The character
  (`PlayerRecord`): the player base's save data by change flag but Factions, and the character parts of the player
  (perks with ranks, skills block, perk points, added spells, base values, items, tints), `Extras` for mod adapters;
  research and the engine's split in `docs/design/join-flow.md`, "Who are you". A new friend brings the character of
  its own game; one the host's save knows joins from the main menu as the kept one (`no_character` otherwise). The
  host keeps records from its guests (on joining, changes, leaving) and writes them into its SKSE co-save (`CoSave`,
  `SKCP`/`CHAR` v2). Guests' saves refused, with a chat line (autosaves once a session; `Save_Impl` 35727 hook).
  Scenarios: `guest-cannot-save` 3 of 3, `host-save-keeps-guest-character` 3 of 3, `guest-brings-character-into-
  hosts-world` 1 of 1, `guest-joins-from-main-menu-as-kept-character` 1 run (join and character pass; the after-scenario
  world diff found 7 QASmoke animated activators `ACTI 1BA5A` at another pose after the load: Loose ends). GameTests
  `[join]`, `[records]`, `[save]`. `+connect_lobby` built (GameTests `[steam]`, not run in Skyrim). Not built yet: the RaceMenu adapter
  (build/join-flow-report.md); the join screen came with the UI workbench (below). Live look refresh of copies: built later the same day (below).
  Later the same day: `guest-makes-character-then-joins` (a new game through the main menu, Quick Start - SE, now on
  in every test game's load order, the race menu driven as a player: Khajiit, female, named, then the join; compares
  the base's save sections, the tints, and the host's copy) passes 3 runs. It found: a copy never had its player's
  weight (no base save section has it; `Weight` now on the announcement and spawn, and in `PlayerRecord`, co-save v3;
  GameTests and the baseline's identity check cover it), and a guest joining from another race kept the host's
  player's tint masks (`ApplyPlayer` now rebuilds them for the race and sex, 40696, before the record's colours).
  Harness: keys work at the main menu (W/S/E are the menus' Up/Down/Accept; arrows and Enter only reach Flash raw),
  `char` types into text fields (`CharEvent`), `menu_var` reads strings, `input_state` shows the control map.
- **The UI workbench merged, the join panel wired (2026-10-24).** `UiWorkbench` (native RmlUi on GLFW/GL3,
  fixtures, `scripts/check-ui.sh`, in `scripts/check.sh`), `ui_models` (party activity, player colors, the
  join model), health faces, the AOL-style chat. In the game the join box follows `JoinService` (steps,
  refusals, Copy and Back by mouse or Escape); joins, departures and a player going down ("has perished.",
  red) are chat lines on every game, not a party panel notice. Scenarios: `join-panel-shows-refusal`,
  `party-panel`, `party-shows-levels`, `chat-reaches-everyone`, `join-refused-shows-why`,
  `guest-joins-by-invite`, `loaded-save-hosts-then-joins-friend` PASS. GameTests now run `SessionService`.
  Later: a new game hosts like a loaded save (hook on `NewGame_Enter` 52118; a game hosting alone steps down to
  join), and a guest that loses its host reconnects (3 tries; Steam rejoins the lobby), else its panel says the
  connection was lost; a kick says so without reconnecting (`join-flow.md`, "A guest losing its host"). The guest's
  network now tells the host closing (code 1) from a kick (2). Scenarios `host-rejoins` (reconnects by itself),
  `host-kicks-client`, the Steam ones and `guest-makes-character-then-joins` PASS.
- **Steam: join from an invite (2026-10-23).** The duo's client game runs as OS user `coop2` in its own
  network namespace, its Steam on a second account (every Steam serves games on the fixed 127.0.0.1:57343;
  `docs/design/testing.md`, "Two Steam accounts"; status `steam` shows each game's account). `SteamService` on
  `ISteamBridge` (Skyrim's `steam_api64`): the host makes a friends-only lobby and invites, the friend's accept hands
  its game the lobby, which connects to the host P2P and joins the session through it (enet tunnelled; the host's
  tunnel delivers into its own listener). `guest-joins-by-invite` 3 of 3 plus earlier runs; GameTests `[steam]`
  (FakeSteam): join, the host leaving, the friend leaving, a foreign lobby refused. A loaded save hosts by itself, a
  lone host steps down to join a friend's invite, the chat says who joined or left or why a join failed
  (`loaded-save-hosts-then-joins-friend`, 3 of 3 after its cleanup fix: a friend's tunnel closing kicks its session
  connection at once). Two lock-order families read and added to `lock_order_known.txt`. Next: `+connect_lobby`,
  then joining from the main menu with the host's save (join-flow stages 4 to 6, not built).
- Placed wildlife copies take the host's chosen NPC base (2026-10-23): spawn and refresh keep the
  exact placed reference, but apply the owner's base and reload its model/race. GameTests reproduces the Rabbit/Fox
  swap, including absent 3D and reversed refresh order; removing either the sent base or refresh handler fails it.
  Skyrim found the reload left every swapped actor disabled: `Actor::Disable` (37267) only queues the disable, which
  landed after the immediate Enable. `SetBaseObject` now calls the reference's own Disable (19801, what that task
  runs). New scenario `wildlife-takes-hosts-pick` (Riverwood's Rabbits, Deer, Mudcrab) failed without it and passes
  4 of 4; Riverwood's desync check is clean.
- Voice lines skip a copy with no 3D (2026-10-23): the guest crashed in `Actor::SpeakSound` voicing a bandit's copy
  it was still loading (`docs/re/crashes/2026-10-23-voice-on-unloaded-copy.md`). GameTests flow fails without it.
- **Quest writes instead of polling (2026-10-23).** The host reads quests once, then only quests marked by
  stage/start/stop/objective events, alias-fill/clear and Reset hooks, or the change funnel. Alias references
  moving or becoming shared invalidate their quests too. Fast tests and the plugin gate pass; restoring the
  timer fails three host cases. Skyrim was not run; quest stage, roles/fills, completion and late join remain.
- **Global writes instead of polling (2026-10-23).** The host reads globals once, then only ids marked by
  Papyrus SetValue (Mod uses it), console/compiled-script assignment and beast-form perk spending. The 250 ms
  timer is gone; the calendar remains HostClock's. The gate passes; restoring the timer fails four host cases.
  Skyrim was not run: global-reaches-guest, late-join-gets-globals and beast perk spending/refunds remain.
- **Actor-value writes and death events (2026-10-23).** The writer funnel (38470), TESDeathEvent and Resurrect
  (37321) replace the value/death scans. Only regenerating pools keep their 100 ms comparison; converged copies
  need no resend. Small hits flush each frame; copies wait for the owner's death-action tick, including a
  resurrection before its delayed replay. The gate passes; the old service/serializer fail eight GameTests
  cases and two wire cases. Skyrim was not run: combat, health, death and rapid resurrection remain.
- **Clear, refund and resurrection coverage (2026-10-23).** Six GameTests cover independent reference/location
  alias clears, both beast perk globals decreasing to zero, immediate revival, and owner/copy resurrection
  refusal. Each fails with the relevant quest/global delta or resurrection-event handler omitted; restored
  handlers pass the full gate (66 GameTests). No Skyrim run or new Skyrim-only scenario was needed.

- MMO settings/password/chat cleanup (2026-10-23): only the host's difficulty goes in the join answer
  and its updates; fixed co-op rules stay in code. Removed the unchecked token/password and extra chat channels;
  dialogue picks still show in chat. Packet fixtures and a two-guest join/change/hold/leave/rejoin flow
  (`session_settings_flows.cpp`). Merged with the character assignment: the join answer carries both.
  `ChatMessageTypes.h` is included by the chat broadcast's header, no longer by every message (`Message.h`).
- Four tiers (2026-10-23, `docs/design/testing.md`, Factorio's model): unit, integration against a fake engine,
  the duo, soak. Tier 1 `ClientTests` (new, `Code/tests/client/`): `PartyRules` (the cutscene share rule and the
  control holds, split out of `PartyNatives`), 45 assertions, rule edit to result 0.4 s. Tier 2 `GameTests` (new
  prototype, `Code/tests/games/`): two games in-process, real `TransportService`/`CutsceneService` and the host's
  rules on a fake engine; 4 cutscene flows in 1.0 s. Skyrim showed the fake's first guess wrong (movement takes
  jumping too, `0x401`). Then GuestScripts' tags and cause rules (`ScriptTags`), WorldEvents' own-event rule and
  a guest's own quests (`OwnEvents`) split out the same way, then NativeReach's code reading (`CodeReach`, Zydis on
  Linux): `ClientTests` 173 assertions. `GameTests` 11 flows: world-state sync between games on a fake world
  (locks, containers, cell state, rejoining, two guests), real `GuestWorldState` and `HostWorldState`, checked by
  `Party::Differences()`. `scripts/xmake-linux.sh` passes clang's flags through config files (CMake packages
  could not build on Linux). `guest-spawns-no-critters` fails when the host's weather drifted to rain (0x4D7FB):
  `CritterSpawn` places nothing then; passes with `fw 81a`. The scenario should set the weather as it sets the
  hour (open). The gate: `scripts/check.sh` (tiers 1-2 and the plugin's build, 3-11 s), run by the pre-push hook
  (`core.hooksPath scripts/hooks`; it pointed at a path that no longer exists, so no hook ran). `coop_test.py
  affected` built: changed sources select scenario tags with their file and glob reasons; an uncovered source fails
  loudly, a `core` change says the full suite covers it and never starts it (`--check`: all 1,183 files under
  `Code/` but tests covered). Shared specs (`Code/tests/specs/`): four cutscene scenarios and two world ones (a lock,
  a chest's items) as data, run by
  `GameTests "[spec]"` on the fake and by `scripts/coop_specs.py` in Skyrim (not run there yet); the fake learned
  `Game.ShakeCamera` for markers; all six pass in Skyrim too (21.5 s). The duo's baseline is pinned (the host's
  weather, noon, date, time scale, players' values and placement; the guest follows by sync, checked before
  cleanup; `raw_days` stays cumulative). `coop_test.py soak`: fresh-duo rounds, seeded shuffles, reports of
  crashes, lock patterns, stalls and memory compared with the last soak; two `scripts` rounds 28/28 each, no
  crash, one unreviewed engine-only lock pattern, the guest's memory 22.9 MiB higher across the two (a candidate,
  not a leak yet). The specs take part in `scenario`, `affected` and `soak`. Tier 2 copies: the real `CharacterService`, `TravelService` and
  `ModSystem` in every game (`ModSystem` reads plugins from `IFormBridge::GetLoadedMods`; the session-id lookups and
  script value conversions moved out of `Utils.cpp`), the host hosting through `HostService`; `copy_flows.cpp`.
  Found and fixed: an NPC unloading on the host in the frame between its assignment and the host's game handling
  the answer left the guest's copy for good (`OnAssignCharacter` now sends `RequestRemoveCharacter`). The `spawn`
  scenarios in Skyrim are still to run on it (the duo was busy).
  Travel flows (`travel_flows.cpp`): the tether both ways, players' copies coming along, NPCs staying with their
  cells; `RunLocalUpdates`' snapshot throttle is per service now (was a function static). New rule in CLAUDE.md:
  every bug gets a test in the fastest tier that can show it.
  Tier 2 actors (2026-10-23): an actor checksum (`Party::Agree`: each owned actor's one copy everywhere, its cell,
  position within 64, dead, enabled, copy flag; held copies as the host's references), `ActorValueService` and
  `PlayerService` in every game, the fake's placed NPCs, health, death, bleedout, respawn and created actors deleted
  with their cell; flows for late join, a host NPC going away (moved, disabled, deleted, back), death and a
  guest's respawn (indoors and out: the fake has exteriors now), a placed NPC the party leaves and returns to, an
  NPC loading as a follower arrives; 24 flows, 7.7-10.2 s; each new flow failed with its covered line broken. Found and fixed: a guest joining elsewhere was never
  brought to the party; an NPC announced in the frame a follower's arrival reached the host's rules never reached
  that follower; the host's arrival sent a guest ahead of it the NPCs of the cell it left; `ActorValueService`'s
  send timers and `PlayerService`'s respawn flag were statics two games shared; a respawn outdoors pulled players
  out of the respawned player's grid (its arrival is `kRespawn` now). Four actor shared specs (`Code/tests/specs/`: an NPC's
  death, disable and trip home, joining elsewhere) with actor steps and expectations
  (`actors agree`, `actor`, `together`, `down`; respawn's spec dropped, parked for the spectate design); pass in the fake, the Skyrim side to map them in `coop_specs.py` (`docs/design/testing.md`, tier 2;
  `copy-contract.md`, "Arrival races"). The fake's unmeasured assumptions are listed for the Skyrim side.
  Tier 2 equipment the same day: `InventoryService` in every game, the fake's equipment (hands, armor slots, our
  equip hooks' events and refusals), worn items in the checksum; flows for a player's weapons and armor, a host
  NPC's weapon, an armor swap (measured in Skyrim) and a late joiner's dressed copy (`equipment_flows.cpp`); items (gold, a dropped sword, a later
  joiner's copy of another guest as it is now, `item_flows.cpp`) and drawn weapons, carried items and hands in the
  checksum. Clock, weather, map markers and time scale the same day (`clock_weather_flows.cpp`, `shared_flows.cpp`);
  magic, effect visuals and AI state (`magic_flows.cpp`); combat and objects (`combat_flows.cpp`, `object_flows.cpp`);
  dialogue's games (`dialogue_flows.cpp`, `FakeDialogue.h`); health, doors, globals and kicks (`values_flows.cpp`,
  `world_flows.cpp`, `session_flows.cpp`). The baseline is done: every working feature has a fast test, details
  left for after the MVP. Coverage per scenario: `docs/design/test-coverage.md`.
- Beast form (2026-10-20, `docs/re/systems/papyrus-guest-scripts.md`, "Beast form"): `client-beast-form-stays-client`
  (3 of 3) shows upstream's beast form bug gone: the change runs on the guest's game only. Fixed on the way:
  a copy respawned for a race change started from the host's record of its owner's last cell entry (stale
  values, graph state and action); the owner's respawn now carries its state (`RequestRespawn::HasState`,
  bridge and encoding tests). Harness: `script_var`, `script_stacks`; `stop` finds games launched from
  another worktree (resolved prefix paths). Passed with it: `death-host-npc`, `late-join-client`,
  `hands-survive-reload-client-player`, `steady-state`, `form-state-round-trips`, the `scripts` tag but the
  two loose ends.
- Players' copies travel with the party (2026-10-14, `df9af2ff`; `docs/design/copy-contract.md`). A copy
  of another player is persistent and moved with the party, never dropped and spawned again on travel;
  the host sends only its actors on an arrival. Found by a host crash in `party-fast-travels`: a deleted
  copy's form id was reused for a Cow, which the copy code then moved, voiced and would have deleted
  (`docs/re/crashes/2026-10-14-reused-copy-form-id.md`); a copy is now the actor at its form id only while
  flagged a copy. Each game kept one actor for the other player through about 30 arrivals (spawned again
  only by the scenarios that disconnect). A copy's inventory is never reset to its base's template
  (`HookResetInventory`, Actor.cpp): enabling a copy's 3D removed and re-added its default outfit, which
  stripped the owner's clothes for up to 3 s (`load-door-not-replayed` failed once on its desync check)
  and sent the removals to the owner. Checked by disabling and enabling a copy: its worn set stays its
  owner's, before the hook it lost all but one item. Passed: the travel scenarios (`party-fast-travels` 3 of 3), the
  equipment scenarios (`equip-*`, `repair-equipment-drift`, `draw-*`, `hands-survive-reload-*`),
  `steady-state`, `late-join-client`, `host-rejoins`, `session-roster`, `host-kicks-client`,
  `move-host-npc`, `death-host-npc`, `party-shows-levels`, `copy-ends-voice-line`, `dive-*`,
  `map-marker-reaches-everyone`, `weather-follows-host`; `BridgeTests` 670 assertions (arrival cases
  rewritten: no player spawns, the host's actors to a player ahead once the host arrives).
- Party tether (2026-10-14, `df9af2ff`): anyone who travels takes the party along. `party-uses-door`
  (the host leads out of the inn, the client back in), `party-fast-travels` (each leads once from the
  river to Riverwood, in one worldspace), `load-door-not-replayed` (the host follows the client out by the
  tether, never by the door), `party-changes-cells`, `party-spreads-on-arrival` (now `party-stacks-on-arrival`), `teleport-keeps-copies`,
  twice each; `dive-*`, `map-marker-reaches-everyone`, `weather-follows-host` (they use `both_go`, which now
  moves the host only). `dive-host-player` 6 of 7: once a lock-order report, a new site of the known
  vanilla `coc` detach, read and added to `scripts/lock_order_known.txt`; once the copy's own gasp (below).
  `BridgeTests` 664 assertions (two tether cases), `TPTests`, `ClientReplayTests`.
  `party-quick-travel` (new): the client goes out of the inn and straight back in while the host's game is
  stalled (SIGSTOP, `game_stalled`). It failed first (the host saw the second travel as "already there"
  while its move out was pending, and went out alone); 3 of 3 since a follower keeps following until it
  reaches the latest place. The travel scenarios again after the fix: `party-fast-travels` once a new
  vanilla lock-order report (an NPC's movement against `Job_Combat_manager`, our pass-through
  `HookApplyMovement` on the stack; read and added to `scripts/lock_order_known.txt`), then 2 of 2; the
  others pass.
- A harness request with a wrongly typed field (`place` with a hex string target) took the game down: the
  command's `nlohmann::json` read threw (2026-10-13). Fixed 2026-10-14 (`3a9c8930`): `HandleCommand`
  refuses it (`bad request: ...`, the field's expected and actual type) and the game carries on.
- Kill moves by the client (2026-10-13, `437a6bb6`): `client-kill-move` 5 of 5 (new). The client's player
  kills the host's wolf with a kill move: on the host the client's character and the real wolf play it as one
  pair ~340 ms later, at the same pace (blows within 50 ms of each other, counted from each start), laid out
  as on the client, and the host records the client's character as the killer. Three fixes
  (`docs/re/systems/actor-ai-and-copies.md`, "Kill moves by the client"):
  - The victim's half on the client: `HookPerformAction` takes a copy's action aimed at the local player
    during the player's own action (never sent). On the host the paired replay carries `TargetEventName`
    when the partner is the host's own actor and starts on the game thread.
  - The host's pair waited ~3.1 s for the copy to reach its mark, which a copy cannot walk to: a copy's
    half now counts as at its mark (`HookUpdateSynchronizedClip`, `BSSynchronizedClipGenerator::Update`
    63050). Without it the scenario fails (the host's blow 4958 ms in against 1999 ms).
  - Kill credit: the owner no longer applies a forwarded hit to an actor held by a kill move (the engine
    gave the pending kill to its local player); the kill move credits its partner. Without it the scenario
    fails (killer 14). `client-melee-kill-credit` now also accepts a blow that becomes a kill move: 4 of 4,
    each ending in one.
  `combat` tag (11), `death-host-npc`, `health-host-npc`, `host-kill-move` 3 of 3 more pass; `BridgeTests`
  639 of 639, `TPTests`. Tools: harness `idle_conditions` (each condition of an idle and its parents,
  `TESConditionItem::IsTrue` 29924) and `kill_move_check`'s `attack_target` (the engine's melee pick 47272).
  Since (`437a6bb6`): `idle_conditions` also gives each idle's event and whether its whole list holds
  (`TESCondition::IsTrue` 29888); conditions and idles in `docs/re/systems/conditions.md`.
- Kill moves (2026-10-13, `5a58a51c`): `host-kill-move` 10 of 10. The host's player kills a wolf with
  a kill move (`KillMoveRandom` 100); on the client the host's character and the wolf's copy play it as
  one pair (the engine's kill-move flag on both in one frame) about 330 ms after the host, laid out as on
  the host (distance, bearing and heading within 15 units/degrees, both kill-move variants), at the same
  pace (the blow within 350 ms of the host's, counted from each start), the wolf's copy dying within
  350 ms of its blow, and the client's own player untouched. Three fixes:
  - Action targets go as session ids (`ActionEvent::TargetActor`): the target was the owner's form id, so
    the wolf's copy paired with the client's own player (form id 14) and the player's copy with nothing.
  - One clock for everyone: a game slowing its clock slows every game's (`HookSetGlobalTimeMultiplier` in
    `BSTimer.cpp`, the host's `HostTimeScale`; each game runs at the slower of its own and the others'
    slowest). Every kill move plays a kill cam, and some of its camera shots slow the clock (`BGSCameraShot`
    `globalTimeMult`, to 0.2 and ramping back): the host's kill move then took 4.1 s against the copy's
    2.9 s, and the wolf's copy stood 1.4 s after its blow until its owner died (3 of 8 runs). Now both
    clocks slow to 0.2 together (2 of 10 runs) and the blows still line up. Slow Time, a bow zoom and
    `sgtm` go the same way. `clock-slows-for-everyone` (each game's `sgtm`, both at once) passes;
    `BridgeTests` (5 `[TimeScale]` cases).
  - An owner does not send its hands while dying (`RunWeaponStateUpdates`): its hands-down arrived before
    its death action, and the copy's weapon convergence sheathed it mid-death (its death 0.5 s late).
  `client-*-kill-credit`, `death-host-npc`, `health-host-npc`, furniture, draw, cast, sneak, quick
  sheathe, `equip-host-npc`, `steady-state` pass; `late-join-client` failed once (Loose ends), then 3 of
  3; `TPTests`. Notes: `docs/re/systems/actor-ai-and-copies.md` ("An action's
  target", "Kill cams slow the clock").
- Review of the session's changes (2026-10-13): every new engine call checked against CommonLibSSE-NG and
  IDA (`SetActorValueModifiers` 38468 as `DoDamage` calls it, the deferred-kill pair through the papyrus
  natives, `HandleHealthDamage` vtable 0x104, `BSTimer` 68245/410196/388442, `CrosshairPickData`, the
  default objects at 0x20 + 8 × index). Fixed: a player joining during a slowdown stayed slowed after it
  (`HostTimeScale` did not record what it told it; a bridge test now fails without the fix), the harness's
  session snapshot sent its action target as a form id, a duplicated `IsInKillMove`, an animation trace
  that failed to start left its screen captures running. `BridgeTests` 639 of 639; on the games
  `host-kill-move` 3 of 3, `clock-slows-for-everyone`, kill credit, `death-host-npc`, `late-join-client`,
  `steady-state` pass, `host-rejoins` 3 of 4 (Lydia's `JumpLand`, Loose ends).
- Animation trace (2026-10-13, `5a58a51c`): `AnimTrace` in `coop_test.py` and the harness's
  `anim_trace` command (`Code/client/TestHarness/AnimationTrace.cpp`). For chosen actors in both games
  it records every behaviour graph event (a sink on each graph's `BSAnimationGraphEvent` source), the
  actions taken, life state, kill move, health, graph booleans and position each frame, with the wall
  clock and frame; both screens; and the plugin logs' lines naming them. It writes one timeline (host
  left, client right), `trace.json`, the videos and side-by-side frames at marked moments to
  `build/coop-test/traces/`. Scenarios can wait on its entries (`trace.wait`, `trace.wait_state`). Each
  game's kill cam and clock speed are entries too.
- Kills (2026-10-13, `5a58a51c`): `client-melee-kill-credit` and `client-spell-kill-credit` 10 of 10
  each, and now wait for the body to come to rest, the copy's within 50 units of its owner's. The
  host takes a forwarded hit through the engine's damage path with the attacker
  (`SetActorValueModifiers` 38468): its death rules, a death animation, the killer recorded at once.
  It used to force the damage without an attacker (the engine queued a killer-less kill) and then kill
  with an instant ragdoll that threw the body and dragged the copy through the air. Copies are in
  deferred kill (as skymp's remote actors): they show their owner's health as is and die only with
  their owner. The client stands in front of the wolf's head.
  `combat`, `magic`, `values` tags 18 of 18; `BridgeTests` 589 of 589. Notes:
  `docs/re/systems/actor-ai-and-copies.md` ("A hit forwarded to the owner").
- Kill cams (2026-10-13, `e4088aca`): `client-spell-kill-credit` passes 10 of 10 (a kill cam in 8):
  the client's killing bolt sometimes plays a kill cam (game clock at 0.25x, player controls off,
  `VATS::mode` 4, 400883), and the reset's one Ready Weapon press fell in it. `takes_input` now also
  waits for the kill cam (status `kill_cam`), and `place_before` for the landing animation
  (`bInJumpState`; a draw pressed in it stalled ~3 s).
- Mod ids (3d, 2026-10-13, `da15a6be`): `BridgeTests` 589 of 589 (ids are places in the load order,
  the same for every player, under each player's own names; written against the old table first,
  which failed on a name differing in case). On the real games 43 scenarios across `session`,
  `restart`, `spawn`, `world`, `objects`, `cells`, `inventory`, `magic` and `combat` pass, all but
  `client-spell-kill-credit` (Loose ends; it fails the same without 3d).
- Player records (3c, 2026-10-13, `f438ba48`): `BridgeTests` 582 of 582 (every test's players now
  join through the real join; the list split out of `HostSession` as `HostPlayers`, and the 28
  passed again on freshly started games); on the real games `session`, `restart`, `spawn`, `world`, `objects`
  and `cells` (28) pass after 3c.1, and `session` and `restart` (11) after the disconnection handler.
  Kicking is covered by `host-kicks-client` only: the headless server has no network peer to drop.
- Player list (3b, 2026-10-13, `75cc39f5`): `BridgeTests` 415 of 415 (who a newcomer learns, with
  the host marked; who learns the newcomer; leaving; a refused game never listed); the `session` and
  `restart` scenarios (11) pass; without the leave notice the bridge case fails, and on the real games
  `host-kicks-client` fails at "[host] no longer lists the client".
- The host's settings (3a.4, 2026-10-13, `755891ca`): new `difficulty-follows-host` (the client
  takes the host's difficulty, follows its change, is held there, gets its own back on leaving; the
  host's is never changed) passes, and fails at the step concerned without the change notice and
  without keeping the client's own; the `session` scenarios (10) pass; `BridgeTests` 389 of 389.
- Plugins and load order with real games (3a.3, 2026-10-13, `5bd486ca`): new
  `join-refused-plugins-differ` (tag `restart`, ~30 s: the client game restarts with an extra empty
  plugin, the real host refuses it, the client's join status lists `extra SkyrimCoopTest.esp`, the
  host's chat says why; the host must drop the killed client within enet's timeout) passes, and
  fails at that step without the timeout handling. The `session` and `restart` scenarios (10) pass;
  `BridgeTests` 378 of 378; `TPTests` pass. Seen in a screenshot: the host's chat line.
- Game versions (3a.2, 2026-10-13, `5bd486ca`): `BridgeTests` 365 of 365 (Skyrim and SKSE
  mismatches, the build checked first, the wording), `TPTests` pass (the new fields round-trip);
  `join-refused-shows-why` adds a stand-in host on another Skyrim and SKSE and a game on another Skyrim
  asking the real host, and fails at that step with the host's check skipped; the `session`
  scenarios (9) pass. The real games send Skyrim 1.6.1170 with SKSE 2.2.6.
- Refusals as data (3a.1b, 2026-10-13, `cf0ad03b`): new `join-refused-shows-why` (the client refused
  by stand-in hosts on another build and full, the real host refusing a game on another build)
  passes; it fails at the step concerned with the old kick, with no refusal status, and with no host
  chat line. The `session` scenarios (9) pass; `BridgeTests` 349 of 349, `TPTests` pass. The scenario
  needs `scripts/xmake-linux.sh build -j16 coop_peer`.
- Join gate 3a.1 (2026-10-13, `cf0ad03b`): `BridgeTests` 339 of 339 (new `session_tests.cpp`), `TPTests`
  pass; the `session` scenarios (8) and `move-host-npc`, `remove-host-npc`, `party-changes-cells`,
  `door-opens-for-both` pass after a hot reload (which joins both games through the new gate).
- Map markers (2026-10-13, `b7b83aad`): `map-marker-reaches-everyone` (place both ways, remove) 3 of 3 on
  a warm duo and 2 of 2 on a fresh one, `chat-reaches-everyone` passes with `key` through the control map, `BridgeTests` 284 of 284.
  Test games had tutorials on all along: `bShowTutorials` went to SkyrimPrefs.ini, which ignores it; it
  is in Skyrim.ini now (the map's first-open tutorial took the next key).
- Relays, object ids, chat (2026-10-13, `b7b83aad`): `BridgeTests` 284 of 284; the `objects` and `session`
  scenarios, `chat-reaches-everyone` and the input scenarios pass.
- Travel and presence (2026-10-13, `87972601`): `party-uses-door` and `party-fast-travels` added; those two,
  the session scenarios, `teleport-keeps-copies` and `party-changes-cells` pass (8 of 8). A full run was
  stopped after 26 (25 pass; the failure, `late-join-client`, is fixed: see Tests and harness).
- Last full run (2026-10-13, `5cdf49d2` build): **47 of 47**. Earlier the same day: 42 of 43, then
  45 of 47 with the new scenarios (the two dives, fixed: they had passed only because the arrival fade
  swallowed the swim input). The morning's baseline was 37 of 43.
- The gate in 4 s (2026-10-23): it took 21 s, 20 of them `GameTests` asleep (0.3 s of CPU). The host's
  listener slept 2 ms on every `Net::Server::Update`, which `HostService::OnUpdate` calls on the host's game frame
  (upstream's server loop thread left it): the host's Skyrim lost 2 ms a frame, fixed. `Server::Close` and
  `Client::Close` waited up to 100 ms for an answer on the frame; they flush the disconnect instead. Test:
  `BridgeTests` "[Network]" (205/100/100 ms before). `GameTests` then took 6.4 s, waiting on the services' real
  timers; `check.sh` runs it in 8 shards (2 s), each on its own ports (`GAMETESTS_PORT`).
- Relayed messages are a template (2026-10-23, ideas.md "Code cleanup"). The 41 headers that only
  wrapped a payload (`Payloads.h`) are `using` lines of `Relayed<Payload, Opcode>` in `Messages/RelayedMessages.h`
  (declared there, for headers that named them ahead; defined in `Relayed.h`); 41 headers gone, about 950 lines net.
  `Opcodes.h` lists every message once in opcode order (`COOP_REQUESTS`, `COOP_NOTIFICATIONS`), which makes the
  opcodes and both factories' `Visit` (`CreateMessageVisitor` is gone). `kRequestRemoveSpell` is
  `kRemoveSpellRequest`, as its type. All 104 opcode values compared equal before and after; `wire_tests.cpp`'s
  bytes unchanged.
- No form id translation (2026-10-23, ideas.md "Code cleanup"): upstream's `ModSystem` mapped each
  client's load order to the server's. The join refuses any guest whose plugins differ from the host's in set,
  light flag or order (`HostSession`), so a form has the same id in every game: `ModSystem`, its table and the
  join answer's numbered plugin list are gone. `FormIds.h` (no state): `ToSession` (none for a created, 0xFF, form;
  0 is `GameId{}`), `ToGame` (0 for a created id), and for the messages that rebuild created forms
  `ToSessionOrCreated` / `CreatedId` (the old `0xFFFFFFFF` id): world state's references (`HostEngine`,
  `ReadContainerContents`), a spawn's reference (`CharacterService`), enchantments, the save buffer's ids and an
  action's target. The 87 `GetServerModId` calls and every `World::Get().GetModSystem()` chain are plain calls; no
  `ModSystem&` goes through the bridges. Handling that changed: a created poison goes without an id (its receiver
  asserted against one); sends that ignored the result carry no id for a created form; guarded ones drop instead
  of sending an id no game finds (a guest's activation, lock or equip of a created form). `ClientTests`
  "[FormIds]". `Inventory.h` includes `<optional>`.
- One line per hook (2026-10-23, ideas.md "Code cleanup"): `HOOK_ID(ID, &Real, HookFn)` (and
  `HOOK_ID_IMMEDIATE`, `Hooks/FunctionHook.h`) replaces the `POINTER_SKYRIMSE` / `Real = .Get()` / `HOOK` triple at
  122 installs in 38 files (266 lines); signatures, ids, breadcrumb names and install order are unchanged. Left as
  they were (24): setup between resolving and installing (`Memory.cpp`'s two, `Actor.cpp`'s `ApplyMovement`,
  `ResetInventory`, `UpdateAI`, `GuestScripts.cpp`'s nine), relocation names an engine-call wrapper reuses
  (`EquipManager.cpp`'s two, `TESObjectREFR.cpp`'s `SetPositionOnAxis`, `SetAngleOnAxis`) and other target forms
  (`ExitProcess`, DInput's device, four `VersionDbPtr` casts). `build_re_data.py` reads the new form: the same 441
  ids, 486 uses and 152 hook rows.
- Event connections in one vector (2026-10-23): `Listen<Event, &T::Handler>(m_connections,
  m_dispatcher, this)` (`Events/Listen.h`, with a sink overload for the registry's `on_destroy` and the ImGui draw
  signal) replaces 123 `scoped_connection` members in 26 classes, each vector where its members were (97 fields).
- `BridgeTests` is `HostAuthorityTests` (2026-10-23): `Code/tests/host_authority/`, `[HostAuthority]`
  tags, the `HostAuthorityTest` namespace; the gate, `CLAUDE.md` and the design docs follow. Dated entries keep
  the old name.
- No character assignment round trip (2026-10-23, ideas.md "Upstream's MMO leftovers"): the host hands
  out every session id and nothing answers an announcement. A player's character id comes in its join answer
  (`AuthenticationResponse::CharacterId`, reserved by `HostCharacters`); the host's game takes its actors' ids from
  `HostCharacters::NewCharacterId` (in-process, inside `RunActorDiscovery`'s existing host check). A game owns an
  actor from its announcement (`CharacterService::AnnounceCharacter`, `AssignCharacterRequest::SessionId`); the host
  drops one under an id it did not give. Gone: `AssignCharacterResponse`, `CancelAssignmentRequest`, the cookie,
  `WaitingForAssignmentComponent`, `InventoryService`'s queue for actors waiting on an id, and `HostCharacters`'
  hold-back of host actors not yet moved; with them the unload-between-announcement-and-answer race. A player
  announced again (respawn) keeps its id; its copies are made again as before. Tests: `HostAuthorityTests` "only
  under an id the host gave"; two host-rule checks that passed with their announcement refused now announce under
  the right id; the copy and item flows follow the new order.
- Cleanup leftovers (2026-10-23): `AssignCharacterRequest` is `AnnounceCharacterRequest`
  (`CancelServerAssignment` is `ReleaseCharacter`); `AnimationSystem::Serialize` lost a check that could not fail.
  Hooks: 20 more installs are `HOOK_ID`, the four explicit `VersionDbPtr` casts included; `Hooks::Resolve<ID>(&Real)`
  resolves one hooked later or only on a condition (`Memory.cpp`). Left: `ExitProcess` (`GetProcAddress`) and DInput's
  device vtable, which have no Address Library id. `build_re_data.py` reads `Resolve`: 148 hooked ids (six call
  wrappers it had labelled hooks by a shared variable name are calls now; four hooks it could not see are found).
  `PlayerNativeResultRequest`, `NotifyPlayerNativeResult` and `NotifyPartyNative` (payload `PartyNativeFrom`) are
  `Relayed` too; bytes unchanged. `coop_test.py`'s source tags cover the new files.
- Mounts work (2026-10-23). A guest's player getting on a copy asks (`MountRequest`);
  `HostCharacters::OnMount` makes the rider's game the owner and tells every game, the rider's taking it
  (`TakeOwnership`, private). Getting off (`Actor::Dismount` 37906 hooked) gives it back (`DismountRequest`,
  `OnDismount`, `NotifyDismount`: the rider's game makes it a copy, `GiveUpOwnership`; the host's takes it). Fixed with
  it: a rider leaving had its horse removed from every game; now it goes back to the host. Every other game's copy of
  the rider gets on at once (`Actor::PutOnMount`: a `MountInteraction` 40345 attached 41532, as InitiateMountPackage
  links a low-process rider) instead of a mount package a copy never runs (it stuck after the ride); it gets off by
  replaying its owner's `HorseExit`, the engine's interaction update unlinking it. A mount's half of a paired action
  is never forced with its rider's (`AnimationSystem`): ownership changes hands mid-pair, the mount's owner plays it.
  A game taking an actor sends its latest action (through `ReadLiveState`'s `TargetToNetwork`: a paired partner as a
  session id, not a form id, or 0x14 lands on each game's own player) and its AI state; a created package (a ridden
  mount's DoNothing, `PutCreatedPackage`) raises the AI state too. Scenario `guest-rides-host-horse` (Riverwood: the
  copy fades in, the guest gets on, both games agree while riding, gets off; 4 of 4). `HostAuthorityTests`
  "Mounts: ..." (the leaving case fails without the fix). Readouts `riding`, `ridden`, `getting_on_off`,
  `mod.fade_state` (nothing activates an actor fading in, `TESNPC::Activate`), `mod.player_can_ride` (38706).
- **A ride is a hold** (2026-11-17, `docs/re/systems/mounts.md`): a horse is its rider's game's only while ridden and
  the host's again once the ride ends, like a held physics body. The hand-back moved from `Actor::Dismount` (37906),
  which only starts a dismount, to `RefrInteraction::Detach` (41539, `HookDetachInteraction`), the end every ride
  reaches: the dismount clip's `FinishDismount` (38696), `StopRidingQuick` (38705: jail, fast travel into another
  worldspace, a load) and Predestroy. The horse went back mid-clip before, and a ride ended without Dismount left it
  the guest's. A host NPC's ride end is sent too. The host keeps each mount's rider (`HostCharacters::Character::
  Rider`): a second rider within the round trip is refused (`NotifyMountRefused`, its player off by
  `StopRidingQuick`), a removed rider's horse goes back, and a joining or arriving player hears who rides what after
  its spawns (`SendRides`; a guest-ridden horse is spawned from the host's copy, `HostGame::CopySpawn`), linked once
  both copies exist (`CharacterService::m_pendingRides`). A player's record takes its relayed actions, so a spawn made
  from it plays the latest. Tests: GameTests `[ride-end]`, `[one-rider]`, `[late-ride]` (the fake's `StartDismount`,
  `StopRiding`, `EndRide`); HostAuthorityTests "Mounts: ..." (one rider, late joiner, removed rider) and "a player's
  latest relayed action". A mount's activation is no longer sent as an NPC activation (the host's replay started a
  mount package on its copy of the player, `kGettingOnOffMount` for good: `Character::ActivateMount` 37864), and a copy
  rider still seated when its owner's ride ends gets off at once (`IActorBridge::ActorEndRide`, as 40352's forced
  path). GameTests "A guest gets on the horse the host just got off" (the fake's `ActivateMount`, `GettingOnOff`).
- A player's active effects go into its record (2026-10-23, `HostCharacters::OnActiveEffects`): spawns
  built later from the record had the effects of its announcement. Test "a player's effects go into its record".
- The actor comparison's "visuals" are what plays on the body (2026-10-23): the owed Skyrim checks
  failed `spec:guest-joins-elsewhere` at "actors agree", the host's copy of the guest's player lacking 0xB94EC.
  That is `FXCameraAttachFluffObject`, which Riverwood's `FXCameraAttachScript` volumes play at the guest's camera
  on its player: the screen's, not the actor's, so no copy shows it (`docs/design/copy-contract.md`). The harness
  now skips visuals whose controller attaches to the camera (`ReferenceEffectController::EffectAttachesToCamera`,
  vtable 0x1A: true for `OwnedCameraEffectController` and `SkyEffectController`, false for active effects' and
  scripts' other effects, IDA 1.6.1170) and shows them as `mod.camera_visuals`. Not a sync bug, so no fast test;
  the fake engine plays no camera effects. To confirm in Skyrim: the rerun was held up by a Steam download
  stalling Riverwood's load (the loading spinner stayed up past the step's 60 s).
- Harness (2026-10-13): a player's button waits for the game to take input (no fade or menu);
  `both_go` waits for the arrival's fade; `attack` waits for the last swing to end; `place` takes a
  bearing; lock patterns match in either order.
- Bridge rules: `BridgeTests` (`Code/tests/bridge`), the host's rules with in-process games on `HostNetwork`, wire
  bytes pinned in `wire_tests.cpp`. Build with `scripts/xmake-linux.sh` (clang 18, lld; since phase 5).
- Encoding: `TPTests` (`Code/tests/encoding.cpp`), same build.

- **Graph stream: get-ups on the stream (2026-11-11).** A knocked-down copy no longer leaves its owner's stream: the
  owner's get-up pick (the clip `hkbPoseMatchingGenerator` chooses from its ragdoll, 59583) is a new stream input
  (`GraphPick`), and the copy's node takes it. `knockdown-lies-as-host` and `knockdown-smoke` pass 4 of 7 each (0 of 3 on dev and
  before), `guest-knockdown-lies-on-host` 7 of 7. The turn left at some get-ups was physics (a getting-up ragdoll is
  still simulated): owners now send their ragdoll until they stand, and the long turn is gone (bugs.md, Get-up). A streamed copy goes
  down with its owner's ragdoll action, not its first body frame; the knockdown scenarios pass 19 of 20. Kill moves
  between two copies are on the stream too (the clip's wait at its mark is a pick); what stays off it is a pair
  with this game's own actor, and any pair but a kill move's (mounts). Was: the copy's body still turns away at some get-ups (its own heading
  from its ragdoll, 39896). Design and what Skyrim taught: [graph-input-stream.md](design/graph-input-stream.md), "A node's pick".
  Next of the same kind: the ragdoll contact event, a pair's scene answers.
- **Graph stream: third full run (2026-11-12).** On `372f2abe`: 348 pass, 40 fail (the second run: 337, 51). The 7
  failures new to the list were rerun 4 times, and those still failing 4 times on dev's plugin: each fails on dev too
  or passed every rerun, so none is the branch's ([bugs.md](bugs.md), "Found by the full run of 2026-11-11", the third
  run). New entries: the host showing a guest's relief box, a party wipe's fight that misfires.
- **Graph stream: one exception left (2026-11-12).** A copy leaves its owner's stream only for a pair with an actor
  this game owns (two owners in one animation). Mount pairs between two copies stay on it (an action writes nothing a
  mount's beside the graph, 39004; `PauseWhile`, `PairSpell`, `KillMoveHalf` deleted), and so does a death (`KillImpl`'s
  action gates nothing after it, and a dead owner's graph updates on: `Actor::KillBesideGraph`). Duo: mounts 16 of 16,
  `kill-move-kills-guest` 20 of 23 where it was 51 of 80. Also settled: "A rider after a spectated kill move" fails 4 of
  4 on dev's plugin, so it is older than the stream. [graph-input-stream.md](design/graph-input-stream.md), "The one
  exception".
- **Graph stream: the final review and a second full run (2026-11-12).** Second full run on `1a4a44a4`: 337 pass,
  51 fail; 15 failures of the morning pass now (`ragdoll-lies-as-host`, `knockdown-smoke`, `knockdown-lies-as-host`,
  `knockdown-front-and-back`, `host-kill-move`, `kill-move-runs-full-length` among them), 17 are new to the list and each
  was rerun twice: 12 passed or are listed shapes, `copy-strafes-with-host` (3 of 3) is the old strafe stutter (in its
  traces since 2026-11-09), two shapes are new entries (the guest player's copy wearing nothing, an extra item in a
  starting kit; neither touches code this branch changed). The code review read all of it: a sync point reaching a
  running copy (three or more players), a state array of any length trusted, and smaller cleanups, fixed with tests;
  one wrong check of mine (refusing sync points with more than 8 sync points) broke `knockdown-lies-as-host` 3 of 8 and
  was removed. Not proven, in [graph-input-stream.md](design/graph-input-stream.md) ("The final review"): two
  machines (every run was one), cost on a crowded host, three players, a switch to stop recording, other mods on the same
  Havok functions. New intermittents in [bugs.md](bugs.md) ("Seen in the final review's duo runs"): a kill move that
  never pairs, a ragdoll that never settles after a failed ride, the guest's ride ending with the horse still its own,
  a knocked-down copy 62 units off, sync points asked for over and over; and the gate failing on `GameTests` now and
  then, uncaught.
- **Graph stream: the full run (2026-11-11).** 388 scenarios on the branch: 339 pass. The 49 failures rerun alone on
  both builds: three were the branch's, one cause, fixed (a copy on its owner's stream never showed hands its owner
  already had up: `hands-survive-reload-host-player`, `hands-survive-reload-client-player`, `late-join-client`, now 2
  of 2 each; [graph-input-stream.md](design/graph-input-stream.md), "Hands on a streamed copy"). The rest fail on
  dev's plugin too or pass alone; all 46 are triaged in [bugs.md](bugs.md) ("Found by the full run of 2026-11-11":
  where each went, twelve new entries, the rest as evidence on existing ones). `join-refused-shows-why` and `join-panel-shows-refusal` needed `coop_peer` built in the worktree.
- **Graph stream: a fight's traffic a quarter of dev's (2026-11-10).** A streamed owner's actions no longer carry its
  whole graph state: ten wolves fighting 15-21 KB/s, dev 74 (`graph-input-stream.md`, Cost measured).

- **Full run and triage (2026-11-16).** 402 scenarios on dev (`0a2f618a`): 364 pass, 38 fail; `scripts/check.sh`
  passes. Each bugs.md entry was checked against the run, three reruns of its scenarios, or the code. Closed (the
  evidence in the entry's place in git history):
  - Passed every run since (4 of 4 unless said): "Greeting and press timing" (`dialogue-guest-picks-at-greeting-end`,
    `dialogue-pick-reaches-host`), "Get-up" (the three knockdown scenarios), "A dropped sword", "A long roll jumps",
    "A wolf's slow rest", "Trade markers", "Sigurd's registration", "A lock order inversion at a late join, on dev"
    (`late-join-client`), "A ragdoll that never settles after a failed ride" (8 of 8, two straight after a failed ride).
  - Fixed in code: "A guest's swing at a moving object" (copies of objects keep their mass, 07e8a37a; GameTests "A
    guest's player hits the bucket the host's knock still moves"), "An old crash reported as new" (`duo_start` forgets
    old crash logs; `test_coop_harness.py` `McpDuo`), "The late save's boot" (`LateAfterDragonslayerBackinSkyrim` boots
    in 36 s).
  - Outdated: "Players' idle variants" and "A guest arriving mid-loop" (players' copies are on the graph input
    stream, their picks streamed; the mid-loop case is GameTests'), "Copy one-offs" (action-replay comparisons from
    before the stream), "The baseline after a death" (its scenario is gone; the death scenarios leave the baseline
    whole).
  - Merged: "A rider after a spectated kill move" into "After a kill move the guest's own body floats" (both
    reproduce 2 of 2).
  - Added: "A revived guest's abilities", "The death scenario's wolf turns on the other player", "A guest that never arrives after a
    fast travel", "A pass-through hook counted as ours" (`HookUpdateAI` past its `IgnoreFrames` size). "Remaining
    validation" became "The Greybeards scene never casts the host's player" (its save boots now; the scenario fails
    at its first step). Kept entries got the run's evidence (crime, rider, the lock pair's path named in IDA:
    12721 > 29619 > `ExtraDataList::HasRunOncePack` 11737; base bytes now naming the factions section).
  - progress.md's Loose ends: solved, outdated and news entries removed (about 60), the graph stream's run notes
    moved here.
- **Full run and audit (2026-11-22).** Dev `aa723513`, one-simulator.md pieces 1-10 merged; `scripts/check.sh` passes
  (7341 GameTests assertions in 540 cases). The 423 default scenarios in 8 batches (the duo freed between): 364 pass,
  59 fail. Then each scene scenario on its own boot (21; `copy-keeps-spell-after-party-travel` and `faendal-letter` on
  `LateAfterDragonslayerBackinSkyrim`): 5 pass; the 4 input ones pass; both late-save ones fail
  (`elder-scroll-vision-reaches-party` on a copy's bow, `greybeards-scene-casts-party` at its first step). The three
  two-account scenarios could not run: both games were signed into one Steam account in this worktree. Every default
  failure ran again in a shuffled order (23 of 59 passed: order or chance), each entry's passing scenario 3 more times,
  and the kill-move-then-ride pairs twice. Run logs: the worktree's `build/coop-test/results/`. Against
  [bugs.md](bugs.md):
  - Closed, 3 (scenario passes and a fast test covers it, or the comparison it failed is gone by design): "The guest
    player's copy wears nothing"
    (`dialogue-picked-text-visual` 4 of 4, the entry's own close condition; `quest-gold-turn-in-guest` and
    `dialogue-options-are-the-partys` pass; GameTests "A quest's outfit rebuild cannot leave a player's copy partly
    undressed"), "A spawn's inventory over a newer equip" (`host-loads-party` 4 of 4, `host-quickloads-party` 3 of 4,
    its fourth a guest crash, no copy without its bow in any; GameTests "An equip sent after the spawn was read
    survives it"). Also gone: "Whiterun plains' critters differ" (piece 10 stopped comparing critters; its scenario's end
    check passed 2 of 3, the leftover horse and elk shapes moved into "A passenger jumps 57 units", which the third run
    showed again), a bullet each of "After a guest rejoins" (the Skeleton: its second clean run), "Failures only deep"
    (`party-aggro-host-first`, `furniture-host-player` 4 of 4), and the index's stale line for "A party wipe's fight
    that misfires" (its section was gone).
  - Unclear, kept, 3: "A strafing copy catches for a frame" and "A player at a chest reads lower than its copy" pass 4
    of 4 where they failed every run, but nothing shows what fixed them and no fast test covers them; "A copy's graph
    after a turn or a pick" (the live check read no run of 2 s apart through the three death scenarios, but no fast
    test has a wolf's copy beside a player's death). The same for
    `guest-swaps-armor-after-join` and `party-buys-horse-guest` (bullets in their entries).
  - Not exercised by the run (no scenario, mods, a dump, or a playthrough flow; each entry says so or has no line
    of 2026-11-22): "A copy's interaction idle", "Animation mods' random picks", "Creature copies wobble", "A
    follower stops following" (its scenario failed on another shape), "A deleted copy in an AI job", "The scratch
    save in the Load list", "Quest start and stop events", "The host's copy after a door", "Players stacked after a
    door", "A guest's crash loading a wipe's save", "A guest crash at the main menu", "A guest's store of an item held
    in a list", "A guest crashed loading a copy's idle clips", "Sync points asked for over and over", "Heap history
    crashes the host at a kill move".
  - Still open, with the run's evidence in each: the other 56, among them the floating body after a kill
    move (4 of 4), the refused joins, the highlight, cover plates, clutter, the crime scenarios (two arrests pass now),
    the werewolf's copy (2 of 2: the stuck transform shows, no crash), a copy without its weapon (still unreproduced;
    a sheathed bow's hand slot differs in two scenarios).
  - New: "A guest crash through a pointer that holds a vtable" (six guest crashes writing through a register that
    holds a vtable's address, measured; the crash sites commented in IDA; freed memory reused is inferred), "A guest crash in the clip-load hook with no World", "A late joiner sees the
    host's copy stuck drawing", "A copy at Sheathed while its owner draws", "The party's sword unnamed at a
    merchant", "A dragged corpse's copy apart from its owner", "A staging spot 1500 units off with no floor" (test
    tools), and "Seen once in the run of 2026-11-22" (ten one-off failures).
## Loose ends

Everything known to be unfinished, unverified or unexplained that is not a bug a player meets (those are in
`docs/bugs.md`). Close an entry by fixing it, proving it harmless, or moving it to `docs/ideas.md`; say where the
evidence went. Details are in the doc named. Triaged against dev on 2026-11-16: solved, outdated and news entries
removed (git history keeps them).

- **A dragged object blinked once in play (2026-11-01).** Not reproduced with `body_trace`; nothing traces a hold; a
  scenario that drags an object while both games trace it would catch a recurrence.
- **Replace the health faces before a Nexus release (2026-11-18).** `Code/client/Ui/assets/moods/*.png` are
  RollerCoaster Tycoon art, not ours and not GPL; swap in our own or CC0 faces.

### Mounts (2026-10-23)

- ~~A copy made while its owner rides starts off the horse~~: built 2026-11-17 ("A ride is a hold" above).

### Mods

- **EngineFixes and our `FormAllocate` hook** (2026-10-22): upstream rehooked it when EngineFixes replaced
  it, but only from its own launcher, so under SKSE the rehook never ran and is deleted. Whether
  EngineFixes' allocator patch overwrites our hook (the heap history and form allocation) is untested.

### World state

- **Why a lid flipped without 3D comes back part open is unread** (2026-11-18, `docs/re/systems/guest-world-events.md`,
  "A lid after a disable"): `InitNonNPCAnimation` (19757) already calls `SetOpenState` with snap at a 3D load, yet the
  new 3D shows 0.116 of the open 0.128 rad. The fix poses it afterwards (`ObjectService`, measured to agree); what
  defeats the load's own pose (the queued load's order, the sequence state the flip leaves) is still to read. Our
  `QueuedReference::Finish` hook (13022) never fired for the chest's job, so its queued kind is unread too.
- **A guest's quest fills are not in its own save** (2026-10-20): `ForceRefIntoAlias` records no change; harmless while a guest never saves in a session (`join-flow.md`).
- **The save's running bounty was not running on one boot** (2026-10-20, `LateAfterDragonslayerBackinSkyrim`): `quest-walk BQ01` found it stopped where earlier boots had it at stage 0; not looked into.
- **Step 5 after slice 1** (2026-10-21; superseded the same day by option 1, under "Tests and harness": a guest runs only its own player's and UI's scripts, so the world-script cases below are closed except what bypasses the gate). Of delayed natives only `Spell.Cast` carries the guest whose script called it (`GuestScripts`, `CastFunctor`); the others do not. Not measured: an engine-only hazard (no script) hitting a guest on both games now hurts it twice (the host's goes to its owner), and a host NPC's hostile spell on a guest now reaches it (the PvP rule covers players' casts only; an NPC's arrow hit the guest in `statue-fires-at-client`), no scenario of its own. The host's messages for a guest's action (`BlessingMessage.Show()` for the guest's prayer) show on the host's game: rule 5, not built. Enabled state (2026-10-21): a guest's own timers and loads can still enable or disable references on its game until step 5 covers them; a guest joining later gets no enabled states (save transfer at join will carry them); actors are left to their owner. Locks (2026-10-21): a guest's own world scripts (timers, loads) locking something are proposals too until step 5 covers them; a guest's lockpicking is not checked against the host's lock (it is taken as is).
- **Containers after slice 1** (2026-10-21, `docs/re/systems/guest-world-events.md`, "Containers"):
  - Counts per base item: an enchanted, tempered or poisoned item taken from a container comes back plain in the other games' containers (the player's own inventory keeps it; actor inventory sync carries extra data).
  - A container's leveled contents roll per game (`InitLeveledItems` at first open, which sends no event): a rolled item the other games lack is taken there as a count of 0, and items only the guest rolled are not in the host's container until the guest takes them. Slice 2: the host's contents when a guest's cell loads.
  - A guest's quick second change to the same item can be undone for a moment: its first proposal's echo (the host's count) comes back after the second, then the second's. Converges; not seen in play.
  - A guest's own world scripts (timers, loads) changing containers are proposals too until step 5 covers them. Merchants' chests go the same way (shared stock), not tested.
- **Quest walks skip what leads up to a stage** (2026-10-20): travel, fights and dialogue between stages are not played; a guest's actions moving a quest come with step 5, inside the same walks. Without a later save the walk takes stages in record order and stops at the first branch that ends the quest (`DA10` at 65).
- **`coop_test.py stop` misses games from a deleted worktree** (2026-10-20): their prefix is under `.claude/worktrees/<name>/build/coop-test`, which `uses_prefix` does not match; they hold the shared game install and the next `duo` waits behind them. Killed by hand this time. Matching any checkout's test prefix would also stop another live session's games.

- **A guest's Days Passed stat stays its own** (2026-10-20): the DaysPassed event fires only from `Calendar::Update` (`docs/re/systems/calendar.md`).

### Copies and actor sync

Notes: `docs/re/systems/actor-ai-and-copies.md` ("Still open"), `docs/design/copy-contract.md`.

- **Open 2026-10-24: a copy's base has no original race.** `TESNPC::Create` clears it (`TESNPC+0x1E8`, upstream's
  "forces facegen"); the owner's is kept. Only a beast form's return reads it, and a copy follows its owner's race.
  Scenarios compare the race section by its race only (`copy_base_differences`).
- **Done 2026-10-22: a host NPC leaving no longer leaves a ghost on the guest.** The removal carries where the host's actor went; the guest's reference follows it (moved, disabled or deleted) and stays a copy until the host spawns it again (`docs/design/copy-contract.md`, "When the owner's actor goes away"; `host-npc-leaves-copy-follows`). Still open: a guest's non-persistent references are made anew when it loads their cell, unflagged, so they run their own AI until the host's spawn arrives (not measured).
- **Done 2026-10-22: a guest's player is in combat when the host's NPCs fight it** (`guest-player-in-hosts-combat`;
  above, overnight slice 4). Not checked: combat music and the wait/save block on a guest.
- **A copy's spawn writes its NPC base's factions** (2026-10-19): `SetActorFactions` (`RemoveFromAllFactions`, then `SetFactionRank`) changes the shared `TESNPC`, so the client records `NPC_ Factions` change forms the host never did (168 in one fast travel). Harmless while the values match the host's; it is client-made world state in its save (`docs/research/world-state-drift.md`).
- **The wolf's copy holds its last pose ~0.6 s after a client's kill move** (2026-10-13): it dies with its
  owner's death action, which comes the host's pair lag (~0.35 s) plus the interpolation delay after the
  client's own `KillActor`. `client-kill-move` allows up to 0.95 s. Watch whether it reads as a stall.
- **A copy's kill move skips the owner's approach** (2026-10-13): on the host `KillMoveStart` comes
  ~180 ms after the paired idle starts (the engine bringing the two together); a copy's comes at once,
  so the client's blow lands ~120 ms after the host's although the idle starts ~330 ms after. Seen in
  the trace; looks fine. The copies spend the first ~250 ms sliding into place by movement sync.
- **Far copies are not in deferred kill** (2026-10-13, review): the engine keeps it only on middle/high
  process actors (`docs/re/systems/actor-ai-and-copies.md`, "A copy is in deferred kill"), so a copy
  dropped to low process can die from its owner's 0 health before its owner's death action. Unseen at
  that distance; the death action is skipped for an already dead copy.
- **Audits not done:** combat, detection, magic/magicka. Then one copy update in place of the
  scattered `IsCopy()` checks.
- **Upstream AI patches to re-check** (from before copies ran no AI; delete each the cut covers):
  `HookIsFleeing`, `HookProcessResponse`, `HookUpdateDetectionState`, `HookIsHostileToActor`.
- **Replay retries:** a copy a graph update behind gets 3 more updates to take an action (as the
  owner's input retries). Logged ("graph updates late"); 1 in 8 casts needed it. Watch the count.
- **Graph state at spawn, not yet covered:** behaviour mods (Nemesis/Pandora, Open Animation Replacer)
  untested. The host's record of a player is refreshed on each arrival (graph state, action, inventory,
  values); between arrivals it is the arrival's.
- **Arrival spacing** needs navmesh under the marker: QASmoke's `coc` marker has none (players stay
  stacked there). Doors and fast travel arrive through the same `TravelService` arrival; no scenario asserts the
  spacing there yet.
- **`TESObjectREFR::GetParentCell` (our virtual)** returned null in an exterior after `coc` while the
  field was set: the vtable slot may be wrong. Nothing of ours calls it any more (the harness's object
  list reads the field, `GetParentCellEx`, since 2026-10-13).
- **Copies converge on their owner for presence only.** State that still arrives only as events (dead or
  alive, inventory beyond equipment, the host's roster of characters, object state) stays wrong if one is
  lost. Add each to the same check when a scenario shows it drift
  (`docs/design/host-authority-migration.md`, "Copies converge on their owner").
- **Outdoors the host counts a player "with" it by worldspace:** a player still far off (same worldspace)
  gets the host's actors there, which the engine deletes; presence recovers them once they meet (an extra
  spawn round trip). Players' copies are no longer affected (they travel with the party). Tighten to the
  loaded area if it costs anything.
- **Copies of the host's placed NPCs come back unflagged after travel** (2026-10-14): the client's copy of a
  river wolf (`DC8DF`, a placed reference) was kept on a fast travel to Riverwood (the arrival keeps copies
  in the same worldspace), and on the arrival frame it was no longer flagged a copy; the identity check
  respawned it as one (correct, one request per copy). Whether the engine made a new actor or something
  cleared the flag is not traced. Before the identity check such a copy came back as a plain actor running
  its own AI.
- **A copy's engine-made inventory changes reach its owner** (2026-10-14): the add and remove item hooks
  send every change not made by us (`ScopedInventoryOverride`) as an inventory change, and the host relays
  those from anyone (looting a host NPC's body changes a host actor). An inventory reset on a copy removed
  the host player's own clothes that way (seen in an A/B with the reset allowed). The reset is closed
  (`HookResetInventory`); any other change the engine makes to a copy's inventory would still go out. The
  remove reason (`ITEM_REMOVE_REASON`) may tell engine housekeeping from a player's action.
- **Which in-game flag keeps a copy out of the save** is not traced: `Actor::Create` sets all of
  `TESForm::inGameFormFlags` (`0xFFFF`, upstream). It works for persistent copies too (checked by booting a
  save made with one, `docs/design/copy-contract.md`).
- **Console `disable`/`kill` on a wild wolf did nothing** on the host, no error logged. Not investigated.
- **Trespass warning AI:** shared party bounty now judges guest crimes on the host and maintains
  guest-copy trespass state (2026-10-26); native warning-AI coverage remains open (`docs/design/shared-party-bounty.md`).
- **Voice lines, not traced:** where a copy's resting `soundCallBackSet` 3 came from in
  `move-host-npc`; why no line is queued on a copy past fade distance. A copy that surfaces before its
  owner's gasp arrives could gasp on its own: seen once (2026-10-14, `dive-host-player`, 1 of 7 runs: the
  host's copy had a line of its own with no length before the owner's gasp).
- **Crash in the harness's `strip`** after 376 hot reloads (`UnequipAll` on a copy, garbage list
  node). Not reproduced on fresh games (5 of 5).

- **An NPC on the guest only** (2026-10-20): after play in Whiterun the guest showed an NPC at a door that the host's game did not; `desync` listed `Ulfberth War-Bear [client 000D15B0, sid -1]: copy in client with no owner in the other game`. Not looked into; the guest's own actors are the copy contract's.

### Engine threading and crashes

- **Freezes and slowness** (2026-11-04): the machine's, not ours (measured 2026-11-05). The export: 2845 frames in
  about 8 minutes, p50 17 / p95 32 / p99 71 ms, worst 2.6 s; `Frame stalled` 70 on the host, 90 on the guest.
  - Shared: 52 of the host's 70 stalls (17.2 of its 23.4 s) hit the guest within 300 ms, the long ones to the ms
    with equal lengths (05:07.447 578/578 ms, 05:10.434 573/574): one thing outside both processes held both. Our
    update was long in 8 host and 4 guest stalls, all at the wipe's load and new game (11:32-11:49).
  - Not the lock inversions: none was live, all are the engine's own orders (`docs/re/systems/lock-order.md`).
  - Measured 2026-11-05 (60 s windows, `profile --route whiterun`): idle, vanilla, our plugin alone and the duo
    all hold 55-60 fps with no stalls; over the route vanilla stalls 10 times a minute (worst 4.2 s), our plugin
    alone 9, the duo's host 8 and guest 5. Stalls are cell loads, and co-op adds none.
  - Left open: what the shared stalls wait on. The guest's logs are silent through its longest ones (11:53 2.5 s,
    12:34 1.8 s). Two games on one GPU and display, plus other agents' builds on the machine, are the candidates.
  - Start: `profile --route whiterun --stacks` on the playtest machine with a game on the desktop (the duo runs
    off-screen, so it cannot show a display stall), during a stall.
  - Test: `profile --budget p95=33,p99=70,stalls_per_min=6`, compared against the same run on vanilla.
- **Done 2026-11-22 (bugs-refactor-plan piece C, rewritten by its step 0): a worn list freed twice.** The guest's
  vtable crashes and the Ivarstead hangs were one engine double free of an item's `ExtraDataList`: a held copy shown
  again (the session's end) has its inventory reset before it has a process, and `RemoveItem` 16059 hands its nested
  call a worn list 19694's unequip freed (`inventory-changes.md`, "the list freed twice"). Fixed by
  `HookUnequipForRemove` (19694) with `FreedWornList`; the graph hooks hold a `WorldGate::Reader`, which the cog swap's
  unload closes and waits for before it takes the `World` down. Tools: heap history counts double frees (harness `heap`, verb `expect_no_double_free`,
  `duo_start heap_history=2`), and a hang on a lock in freed memory names its free. `guest-ivarstead-fine-mouse-sweep`
  with the quarantine: hung 3 of 3, then passed 2 of 2. The copy teardown between frames was dropped.
- **Done 2026-11-22: a copy's clip request deadlocked with the loader's callbacks.** Found by the same runs
  (`guest-ivarstead-jail-mouse`): our replay asked for clips inside the graph's update, its graph lock held, while the
  animation file job held the locks a request takes and waited for that graph lock in an equip's callback
  (`animation-file-loads.md`, "A request under a graph's lock"). The stream's request is now made between frames
  (`DeferredClips.h`).
- **Done 2026-11-01 (C1, the 2026-10-31 playtest): players can't loot each other.** Taking a dead guest's
  clothes crashed the host in `ContainerMenu`. Now `ContainerMenu::OpenMenu` (51140) refuses another player's copy
  (`party-items.md`). GameTests "Players can't loot each other" passes. The duo scenario `host-cannot-loot-guest`
  recreated it unfixed (the loot menu opened) and passes fixed. The duo's guest is unnamed and `ActivateRef` (19796)
  activates nothing nameless, so the scenario names the guest in the race menu first and restarts it after. One
  fixed run timed out waiting 8 s for the body to rest (passed on the next run; not masked). Found while writing it: in GameTests, the host's death doesn't
  kill its copy on a guest's game (fake gap or real bug, unchecked).

- **Host crash freeing a behaviour graph in a guest's beast form** (2026-10-23): noted, not investigated (`docs/re/crashes/2026-10-23-guest-beast-form-graph-gc.md`).
- **Host crash in `TaskQueue::Drain`, 2 s after a save** (2026-10-19): the Runner queue looked overwritten; not reproduced in six more saves (`docs/re/crashes/2026-10-19-runner-queue-after-save.md`).
- **Our client update runs on a job thread** inside `HookVMUpdate`. Which AI stage jobs run beside it,
  and are our engine changes (spawn, delete, `MoveTo`, packages) safe there?
  (`docs/re/crashes/2026-10-12-wolf-combat-coc.md`, open questions.) Not verified in IDA either: which
  `WaitForJobTask` joins `Job_Combat_and_magic`.
- **Wolf crash fix rests on the code path only:** the one-game repro never crashed (0 of 10 with and
  without the fix); the two-game load is needed to hit it.
- **Menu mode and `equipitem`:** an NPC's `equipitem` run while paused never shows
  (`AIProcess::Update3DModel_Impl` reads menu mode; not traced). Why the harness pauses only cell changes.
- **Lock order, vanilla:** `coc` while the exterior world steps inverts the extra-data and `bhkWorld`
  locks. Listed in `scripts/lock_order_known.txt`, not fixed.
- **Host hang in `dialogue-refused-start` (2026-10-22, the 25th scenario of a `--tag ai,combat,magic,spawn,session,scripts`
  run; 3 of 3 alone).** The main thread waits for jobs in `Main` (36555); one job thread waits in `Mutex::Lock1`
  (13129, the engine's `BSSpinLock`) from `MenuTopicManager` (35289) → `Actor::UpdateInDialogue` (37541) → our
  pass-through `HookSpeakSoundFunction` → `SpeakSound` (37542) → `SetupSpecialIdle` (39256) → `sub_1406F0790` →
  `sub_1406DE5E0`, for the speaker's (Lydia's) `MiddleHighProcessData+0x1E8` spin lock. No thread in the dump is in
  one of the eleven functions that take that lock (`sub_1406DFBC0` ... `sub_1406F1020`, all scoped), the main
  thread is in Main's own wait: the lock looks leaked or its memory reused (Brenuin, her foe, was disabled and
  deleted 0.2 s before the host spoke to her). Not reproduced. The lock-order checker covers only
  `BSReadWriteLock`, and `BSSpinLock`'s unlock is inlined, so it cannot name a holder; instead the HangWatchdog now
  logs, for a thread waiting in `Mutex::Lock1`, the lock (its `rdi` in that frame) with its owner thread id and
  count and whether that thread exists (`CrashHandler::SetSpinLockWait`). Next time: an owner that is a listed
  thread names the holder, one that does not exist means a leaked lock or freed memory (then
  `repro --heap-history` on the process data).
- **Portal graph crash:** our fix works on the repro; SCN-Crash-fix was not run against it
  (`docs/re/crashes/2026-10-11-qasmoke-exit-portal-graph.md`).

### Migration

- **Tether gaps** (2026-10-14, `docs/design/host-authority-migration.md`, "Party tether"):
  - Ready for v1.0.0 testing, accepted by the user 2026-10-27. Remaining observed travel issues are open in `docs/bugs.md`; validation details and retained limits are in `docs/design/party-travel.md`.
  - Departure announcements built 2026-10-27 (`party-travel.md`): 40438 observes queuedTarget before loading, including coc's inline queue. Followers no longer wait for arrival. Actual load starts retain network/frame skew (21–40 ms locally); cached 8 ms loads can still finish before a follower starts.
  - Guest fast-travel time fixed 2026-10-27: the native calendar step is sent to HostClock, which advances the host once and shares the result. A three-player midnight flow and the native fast-travel scenario cover it.
  - Every travel of a player's own leads: a scripted move on a client (a quest teleport, jail) takes the
    party along too. Within a cell or the loaded grid nobody moves. Not a respawn (2026-10-23): upstream's
    `RespawnPlayer` moves the player to its cell's COC marker, which outdoors pulled players more than two cells
    away; that arrival is `kRespawn` now and leads nobody (tier 2 "Outdoors, a guest's player goes down").
  - Two players leading at once to different places can end apart (each game follows in the host's
    order; a leader may follow the other one afterwards). Not seen; the fix would be the host keeping the
    party's last destination and sending a follower that arrives elsewhere on to it. One leader travelling
    again before the others got there is handled (`party-quick-travel`).
  - A follower whose tether move never arrives stays "following" for 60 s: a travel of its own in that
    time does not lead.
  - A player joining somewhere else is pulled to the host's player and pulls nobody (2026-10-23,
    `HostCharacters::BringToParty`; tier 2 late-join flow, `BridgeTests` "a player joining away from the host").
    Outdoors also when out of the host's loaded grid in the same worldspace (fixed the same day after Skyrim's
    four-cell join: `InHostGrid`).
  - Players walking apart outdoors work (measured 2026-10-23): the far copy is kept without 3D and loads again on
    return. A far respawn's new copy is lost out of the host's grid and not sent again (respawn parked).
  - Menu/dialogue/dead/mounted follower cases and three-player fast flows added 2026-10-27; evidence and limitations: `docs/design/party-travel.md`.
- **Build ids of uncommitted trees match each other.** `BuildCommit()` is `git describe --tags
  --always --dirty`, written on every xmake run (`Code/common/xmake.lua`; it was configure-time only
  and lagged commits until 2026-10-13): two different uncommitted trees on the same commit both read
  `...-dirty` and pass the gate's stage 1. Fine for development; release builds come from commits.

### Tests and harness

- **Open 2026-11-17: the graph stream check crashed a guest.** `guest-trips-platform-guard`'s first run: the guest's
  game died 48 s in, in `GraphStreamCheck.cpp`'s `HookHandleEvent` (line 113), formatting the platform guard's copy's
  event sender name: the sender read at event +0x10 named a pointer to `0x40C000003F800000` (two floats, not a node).
  Once in four runs; crash log in the guest prefix's `SKSE/crash-2026-11-17-07-32-59.log`. Not read in IDA: what an
  event's +0x10 holds when it does not come from a node.
- **Open 2026-11-07: the main loop stops under the Anniversary Edition box.** With the box (52156) open outside a
  session, the plugin's main-loop heartbeat stopped about 2 s later in every such boot (no crash log). The harness now
  keeps the box from queuing (`bFreebiesSeen`), so nothing hits it; not read in IDA
  ([message-boxes.md](re/systems/message-boxes.md), "The test games").
- **Open 2026-10-24: the race menu's Weight slider, driven by harness keys, moves but the base's weight does not.**
  `ChangeWeight` (`sub_1409524D0`) writes `TESNPC+0x1FC` only when the value differs from what the menu stored; with
  D presses the slider reached its top in one step and the weight stayed (50, or 75 once). Not understood;
  `guest-makes-character-then-joins` leaves the weight at its default and still compares it in every game.
- **Checked 2026-10-22: a copy that swam stops swimming with its owner.** `copy-leaves-water-with-owner`: the host
  swims in the river and is put on its bank; its copy on the client swims, then stops (2 of 2). Not covered: an
  owner walking out up a shore (the copy follows at its owner's pace there).
- A placed iron ingot (`created_reaches_guest` with `5ACE4`, by hand) came to rest with changes `0x3` on the
  host and `0x2` on the guest (2026-10-21): a form-flags record (`0x1`) the guest's twin lacks. Not followed
  up; the scenario places the cell's ladle, which never showed it.
- **Guest scripts, option 1, built 2026-10-21** (`docs/research/guest-scripts-and-mods.md`): a guest runs only the
  scripts its own player or UI caused (and what they start). Open: the UI lane is untested (no SKSE script mod in
  the test install); fragments and stacks resumed from a save skip the gate (no `SendEvent`); a guest's player-alias
  events (location changes, item pickups) are dropped and not yet raised on the host (party model rule 4).
- **Quest fills, 2026-10-21:** a fill of a reference the guest never loaded is read from its plugin as the host's
  quest did (15328), `quest-fill-loads-on-guest`; a created reference in a fill maps to the guest's twin
  (`quest-roles-match-host` now compares them by base); location aliases are synced (`QuestState::Locations`,
  25054/25067), no scenario (no console-started quest fills one in the test setup). A join loads every running
  quest's unloaded references at once: not measured on a late save.
- Not done from the 2026-10-21 list: enchanted, tempered or poisoned items taken from containers still arrive plain
  (counts per base item); the client sometimes misses one door `open` event in `world-changes-reach-guest` (2 of
  ~10 runs, passes on rerun), not looked into.

- Rule 2 through `SetStage` has no scenario (2026-10-20): `vilkas-counts-client-hits` now claims rule 1 only (the trial's sword is given at stage 200, from dialogue). Candidate: Northwatch Keep's `MS09NonCombatTrigger1Script` (`OnTriggerEnter`: `Game.GetPlayer().SetFactionRank`, then MS09 stage 55, whose fragment ranks `Game.GetPlayer()` too); needs a faction readout and MS09 staged at 40. `statue-fires-at-client` checks the host's own hits now and fails with rule 2 off (`docs/re/systems/papyrus-guest-scripts.md`, "Rule 2 never ran").
- The client's fire statue in Soljund's Sinkhole fired though `6AE8C.disable` ran on the client (2026-10-20): its script checks `FPStatueA.isEnabled()`. Not followed up; the scenario no longer disables it (step 5 stops the client's trigger).
- The guest's view of a script's object animation (a claw door's ring turning, from the host's `NotifyObjectChanges`) has no harness readout: `ring-turns-for-client` checks that the guest's own ring script stays, not that its ring turns (2026-10-21).
- `load-door-not-replayed` reported a lock-order inversion once (2026-10-20 night, host: `+0xe85437 > +0x17928d` against `+0x16abbd > +0xe92c16`, ours on the stack), next to known patterns; passed on rerun. Stacks not read.
- `Projectile::LaunchData` (`Games/Skyrim/Projectiles/Projectile.h`) names `+0x50` `fYAngle`; CommonLibSSE-NG has a pointer there (`unk50`) and `desiredTarget` at `+0x58`. Projectile sync sends it as `YAngle`. Not checked in IDA.
- A failed `sword-box-asks-client` run leaves the secret door open on the client, so the next run fails at "not closed to begin with" until a fresh duo.
- The lock-order checker reported two inversions on the guest during a beast form change (2026-10-20, `tp_client-client.log`: the guest's character's reference lock, `SkyrimSE.exe+0x16d4cb > +0x182241` against `+0x182315 > +0x17928d`, one marked ours). Not followed up.
- A release-mode client build (`-m release --test_harness=n`) fails to link: `/opt:ref: no such file or directory` (2026-10-19, seen while checking a player build; `-m dev --test_harness=n` links).
- A stuck `LoadWaitSpinner` once kept the host from taking input (2026-10-13, a fresh game): the first
  `cow` to the river took over `both_go`'s 10 s fade wait on the client, and the cleanup `coc` ran while
  the host's spinner was up; it stayed through later cell changes until a restart. Not reproduced.
- `party-uses-door` once saw the host's activation of the inn door (13419) do nothing for 60 s, with no log line
  on either game during the step (2026-10-22, after a hot reload); it passed twice right after. Unexplained.
- The stuck `LoadWaitSpinner` happened again (2026-10-13, host, the first `both_go` to Riverwood on a fresh
  duo); a restart cleared it. Third time 2026-10-21 on the client, from `host-kicks-client`'s rejoin onward
  (every later step waiting for input timed out); a fresh duo ran kick, late join and the failed scenarios
  clean. A harness readout of what opened the spinner would catch it next time.
- The map's fast travel UI (open the map, pick a marker, confirm) is not driven; `party-fast-travels` uses
  the console's `FastTravel`, which queues the same move (`docs/re/systems/player-travel.md`).
- The `duo` harness process once segfaulted on exit after a pass.
- Harness console lines other than cell changes run unpaused, unlike a typed line.

## v1.0.0 features (decided 2026-10-24)

What the first playtest build has besides what works today (the table above):

1. **Death: spectate, then respawn beside the party** (`docs/design/death.md`, `.worktrees/death-spectate`):
   built 2026-10-24; host authorizes recovery after 10 continuous seconds clear, spectator camera/UI and
   party-wipe checkpoint reload replace the old 5 s/COC respawn. GameTests, UI fixtures, plugin build and the
   host-death, guest-death and wipe Skyrim scenarios pass. Fixed stale copy magicka and recovery graph state.
   Ragdoll physics/settling is feature 5's integration; report: `build/death-spectate-report.md`.
   Real death since 2026-10-26: the player really dies in a session (mods' `OnDeath` fires) and is
   revived. The guest's place after a wipe and `death-spectate-guest`'s staging fixed the same day (`death.md`).
2. **Shared party bounty** (`docs/design/shared-party-bounty.md`, `.worktrees/shared-party-bounty`):
   merged into dev, 2026-10-26. Host witnesses judge guest actions; violent/nonviolent bounty and
   infamy and native player-enemy flags mirror per faction. Guest bodies expose shared bounty and
   known crimes to native queries; host NPCs apply the party's crime reactions to guests too.
   Guest fines debit the chooser once, confiscate on its own game and end guest-body hostility/combat.
   Review adds guard arrest/jail: a guest's real theft opens vanilla arrest choices, its fine charges the
   party's purse (since 2026-10-27), and resisting makes the guard fight that guest. A jail choice runs the native sentence
   on the host; the existing tether brings every player into the same locked cell (no new jail travel).
   Fixed guest arrest greetings, stale option lists and locally rendered fine text; engine layouts are typed.
   Authority/encoding tests, seven crime/dialogue flows under both lossy profiles, UI and plugin build pass.
   Native guest arrest/fine/resist/jail checks PASS. The host jail control reaches the sentence and brings
   the guest, then reports an existing physics lock inversion during release. Six dialogue and nine older
   crime regressions PASS; pickpocket's final world comparison fails on a QA ladle pose. The final merged
   gate passes all 115 GameTests, authority/encoding checks, UI fixtures and the plugin build. Prior native
   physics failures and review evidence remain recorded in the design doc.
   No existing Games/Skyrim hooks or death/body files changed.
3. **Quest rewards to everyone** ([quest-rewards.md](design/quest-rewards.md); built 2026-10-27 with feature 11,
   merged into dev). Skill XP inputs, skill levels, perk points, perks/reversals and
   permanent stat changes reach every player as the party's calls (the reward natives relayed, `dragonborn.md`).
   Body/effect/equipment gains and a picked line's Speech remain personal; items and gold stay with the picker.
   A host pick is tagged before its begin fragment runs. Native quest stage/completion events reach guests without
   their reward fragments. No catch-up ledger; earned permanent stat totals are kept in character records.
   Calibration exposed the old combat XP relay double-applying rewards; removed, and 40488's ABI fixed.
   Validation: full gate PASS (142 GameTests, 57 ClientTests, 164 authority cases, encoding/UI/DLL);
   both lossy reward/record profiles PASS; six real turn-ins plus two native checks 3/3 shuffled (24/24), full
   character rejoin PASS; Speech/training/shrine/beast/dragon/personal-XP regressions 6/6 PASS. Duo stopped/freed.
   The known plate-carry flake failed earlier and passed the final gate. Owners receive shared perks, but a
   copy's HasPerk still reads its own missing list; guest start/stop events are not guaranteed completion events.
   Both limitations recorded in `bugs.md`; Experience DLL not installed/tested. No commits.
4. **Shared wait and sleep** (`docs/design/shared-wait-sleep.md`): built 2026-10-26. Any player's wait
   or sleep opens every game's native menu; the host's countdown drives them all. The party-event
   follow-up makes a guest's sleep run the matching native rest on the host's player too, so host scripts
   receive sleep start/stop; engine sleep callbacks use that host-player context. Vanilla listener/player
   effects are recorded in the design doc. Fast tests, native/UI/DLL checks and both lossy profiles pass;
   follow-up rest scenarios pass 3/3 shuffled (18/18), including the host's vanilla sleep-script bonus.
   Calendar/world/script-context regressions pass; physics regression limitations are recorded in the
   design doc.
5. **Physics:** objects and ragdolls from the host, live (`docs/design/physics-sync.md`): steps 1-6 built 2026-10-24.
   Owners built 2026-10-26 (`.worktrees/physics-owners`): every game streams the bodies it simulates
   (one `IBodyBridge`, `BodyService` in every game), a guest's player's ragdoll reaches the others, and living
   knockdowns knock the copies down with their owners. A dead body's pose at a cell load is parked in
   `docs/ideas.md` (it drops in place on the guest, lootable).
6. **Dropped items** (created references), with their extra data; items in chests keep theirs too. This is how
   players give each other items in 1.0 (a trade menu comes later). Built 2026-10-26, merged into dev after a
   review ([dropped-items.md](design/dropped-items.md)): a guest's drop is made by the host's game
   and the dropper's own reference becomes its twin (its fall its own until the host's rest); a guest's pickup asks
   the host, the first request wins and the item is gone for everyone; the host's own drops and pickups are its
   world's. The item's enchantment (player-made too), charge, tempering, poison, soul and custom name travel
   (`Inventory::Entry`, name added), and containers' changes carry their stacks with extra data. A guest's drop where
   the host's game has no cell loaded waits for it. Tiers 1-2 pass (`[Items]`, `[loose]`, both lossy profiles);
   scenarios `host-drop-reaches-guest`, `guest-drop-reaches-host`, `guest-pickup-race`, `chest-keeps-tempered-item`
   3/3 shuffled. Skyrim found two engine gaps, fixed: a deleted reference was still picked up (both players got the
   item), and a stack with extra data added with a count needs its `ExtraCount` (else one plain). The review fixed a
   player-made enchantment comparing unequal across games (chests rebuilt such stacks on every sync), a drop the
   host's game cannot make (now told, stays the dropper's), and a pickup in the frame its drop was made.
7. **Followers:** on dev ([followers.md](design/followers.md), 2026-10-27): shared vanilla slots; recruiter/latest Follow picker controls native pathing; death/disconnect fallback and return; host save bindings and picker inventory. V1 keeps vanilla Wait → Follow. Native acceptance and remaining travel/favor/combat checks are recorded in the design.
8. **Vampirism:** each player's own, the infection and the hunger (the user, 2026-10-28). Built 2026-10-28
   (`.worktrees/party-scripts-2`) on per-player system quests, [system-quests.md](design/system-quests.md):
   a call from a player's own script into a quest's function makes the quest one per player on every game (told
   through the host, kept in the save's co-save); a call into one runs on the instance of the player behind it, the
   story's on every game (the cure cures every vampire); its stage and its scripts' globals stay out of the world sync;
   a guest's instance is kept in its record. Hooked at the call itself (`ScriptFunction::Call`, 105466). It replaced
   the shared vampirism built earlier the same day ([vampirism.md](design/vampirism.md)). Tiers 1-2 pass. Skyrim
   `vampirism-is-each-players`: see the run in the session's report.
9. **Cutscenes, step 2:** each player's own character in a scene. Built 2026-10-27 (`.worktrees/party-scripts-2`):
   a Body row in the router's engine table (moves, idles, AI drive, read from code, a queued functor's
   vtable too), so a story's call on the player's body runs on each game's own player and a guest's moment on its
   own game; a mirrored move leads no party travel (one check in party travel's `HookProcessQueuedTravel`); each
   guest's player plays the host's player's part in an engine scene (`GuestScene`). Tiers 1-2 pass; Skyrim scenarios
   from late saves: [cutscenes.md](design/cutscenes.md), "Step 2". `Debug.SendAnimationEvent` read 2026-11-07: the
   code reader follows a native's first argument (r9) and resolves its graph holder's slot 1 (38048), so a story's
   event on a player plays on each game's own player, on any other reference on the host only (ClientTests
   `code_reach_tests.cpp`, GameTests `cutscene_flows.cpp`; [party-calls.md](design/party-calls.md)). Checked in Skyrim:
   spec `story-animation-event-reaches-guest` (recorded through the MCP; a host script's event on the guest's
   character bleeds out the guest's own player).
10. **Mounts:** ready for v1.0.0 testing (user accepted 2026-10-27); on dev: host/guest riding, exterior border crossing, dismount and disconnect; a following rider's horse accompanies it and native travel's implicit dismount returns ownership at the horse's latest position. [Party travel](design/party-travel.md).
11. **The party is the Dragonborn** ([dragonborn.md](design/dragonborn.md); built 2026-10-27 with feature 3,
    merged into dev): a word of power a script teaches or unlocks every player has; every soul
    gained reaches each player's own count (souls are each player's to spend: a Magic menu unlock is the spender's);
    the story's factions and spells on a player (titles, the Companions' blood, marriage's factions) reach every
    player, a player's own moment (its beast form's quest) stays theirs; an NPC's relationship with a guest's
    character is the player's (0x7), as ownership. Rebuilt the same day as the party's calls (dragonborn.md, "The
    party's calls"): the story's call of a native changing a player's progression (a row of `PartyRules::ProgressOf`)
    is relayed as the call and runs on every game's own player (`PartyNatives::Scope`, `RouteOf`), replacing the
    per-kind hooks, `PartyChange` and its apply; feature 3's rewards are more rows. A rejoining character keeps its own souls and soul-bought words
    (`PlayerRecord::Words`, value 133), and the save's shouts come along. A dragon's death is the world's event:
    its dissolve and the absorb's streams show on every game, the streams on each game's own player (`Call::kShow`,
    `LocalForm::EachPlayer`). Relayed calls now pass objects as their parameters' types: the VM had dropped every
    mirrored effect on an actor (`GlobalCall`). Tiers 1-2 and both lossy profiles pass; Skyrim: the dragonborn and
    reward scenarios and their regressions, 21/21 after the rebuild.
12. **Script rules 3 to 5** (`party-model.md`): the host's messages (notifications, message boxes) on every game;
    position questions without a cause ask about the party; a guest's story events (locations, crafts, kills) raised
    on the host for its copy. Rules 5 and 3 built 2026-10-27, merged into dev. First the party's
    calls became one router ([party-calls.md](design/party-calls.md)): every native's call is routed by one decision
    (`PartyRules::RouteOf`) from what its code reaches, read once with Zydis, its virtual calls resolved against the
    player's vtables; the progression name list became engine functions with the party's policy flag, and three relay
    messages one (`RoutedCall`). Behaviour unchanged: 24 of 24 regression scenarios. Rule 5 built: the story's messages
    on every game, a player's own on its game, a story's box answered by the host with the others' copies closing
    (`story-message-reaches-party`, `story-box-host-answers`; a guest's lever's box is the guest's now:
    `relief-takes-client-relics`). Rule 3 built: a quest's question about the player asks the party (yes/no: any
    player; else the player nearest what it names; `timer-asks-party-position`). Rule 4 built 2026-10-27
    (party-calls.md, "A player's story events"): every story event a guest's game raises (any type: arrivals, crafts,
    skills, levels, script events) is sent to the host and raised there for its character, its members read from the
    engine's type table (generic since 2026-10-28, `.worktrees/story-events`; one naming another game's actor, a kill
    or an assault, is the host's own), and the story manager's visitors judge the guest's character while one of its
    events runs (a copy's kill too); a party arriving together is one arrival. HostAuthorityTests, ClientTests, GameTests; Skyrim
    `guest-story-starts-no-quest`, `guest-location-change-starts-story`, `guest-craft-raises-story` pass. A copy left getting up after its guest leaves the
    crafting menu (2026-10-27) was not seen again (2026-11-18, `guest-leaves-crafting-mid-craft`).
13. **Party travel gaps:** ready for v1.0.0 testing (user accepted 2026-10-27); on dev: guest fast-travel time advances HostClock; native guest MoveTo leads the party; queued destinations are announced before loading. Menu, dialogue, dead/mounted follower and three-player coverage added. Full gate and both scoped non-death travel/mount network profiles pass; remaining jail cleanup, intermittent lock reports and death/physics failures under packet loss are open bugs in `docs/bugs.md`. Native measurements, validation and remaining limits: [party-travel.md](design/party-travel.md).
14. **Small visible bugs** (built 2026-10-27, merged into dev): the same resurrected NPC
    copy draws and attacks again; a brought character gets checked placement beside the host; a copy's base level
    follows its owner; instant FX Persist effects flash; looks update the existing actor and base. Fast tests were
    made to fail first. All nine affected Skyrim scenarios pass shuffled, NPC resurrection also passes three
    consecutive runs, and player recovery passes alone after one wolf-target staging failure. Both lossy network
    profiles pass 15 affected flows. Validation: [small-bugs.md](design/small-bugs.md). The final gate's only failure
    is the plate-carry physics case, also reproduced on unchanged dev `241bb0f2` under the same shard order (75 units
    behind, about 20 allowed); source and logs are retained in the worktree's `build/baseline-dev/`.
15. **Menus that pause the game** (merged into dev 2026-10-27; [pausing-menus.md](design/pausing-menus.md)):
    decided with the user that personal menus keep the world running.
    The host's map, journal, crafting, lockpicking, barter, books and training leave its world and guests' NPCs live;
    a guest's menus leave its own simulated player live too. Map/local-view rendering and first-person return are
    handled, books render unpaused, and journal saves run between frames on the main thread. Shared rest/loads keep
    their pauses; offline menus stay native. Three unit cases, all 18 native menu scenarios shuffled, three marker
    regressions, shared rest, host loads, guest load refusals and dialogue training pass; plugin/UI checks pass.
    All 132 other GameTests pass; the gate's existing plate-carry physics failure also reproduces on unchanged dev.
16. **The host's loads take the party along** (built 2026-10-26; [host-loads.md](design/host-loads.md)): host
    quickload, menu and console loads send the chosen save to every guest through the wipe's reload and rebuild
    the host's rules on the same listener. Each guest rejoins as its saved character at its saved place. A guest's
    own loads are refused in chat, and a refused menu load releases its journal and loading fade. Quickload
    bypasses `Load_Impl` (35728): it queues `0xD0000400` through 35769; its exact quicksave comes from the engine's
    per-character `SaveEntry` (35789). GameTests cover three guests, an older chosen save and repeated loads;
    Skyrim scenarios cover quickload, the Load menu, console loading, guest refusals and the shared wipe.
17. **The party's purse** (decided and built 2026-10-27, merged into dev;
    [party-purse.md](design/party-purse.md)): every player carries one purse, the host's player's gold. Any
    player's gold change (loot, trade, training, a fine, a reward on a guest's copy) reaches the host as before
    (`RequestInventoryChanges`), which keeps it in its player's gold (`HostPurse`) and sends the purse to every game
    (`NotifyPartyGold`); each game sets its player and its copies of players silently, a copy whose spawn items land
    later too. A joining guest carries the purse, not what it brought. Items and quest item rewards stay each
    player's. Tiers 1-2 (`purse_tests.cpp`, `purse_flows.cpp`, join and records flows) and both lossy profiles for
    `[purse],[items],[records],[crime]` pass; the host-load flows fail under the lossy profiles on dev as well.
    Skyrim: `party-gold-is-one` and the gold scenarios that assumed each player's own (five dialogue,
    `chest-take-all`, `host-save-keeps-guest-character`, `late-join-client`, the crime fines) pass; the crime
    scenarios' witness and arrest-travel steps fail now and then, before any gold, on a build without the purse too.
17. **Friendly fire, always on** (`docs/design/friendly-fire.md`; built 2026-10-26, `.worktrees/friendly-fire`):
    players hurt, knock down and kill each other as NPCs would. A player's weapon hit on another player's copy is a
    request to that player's game (`HitRules`, the damage hook's rule), and hostile spells and shouts reach other
    players (`IsBlockedPvp` deleted). The victim's game applies a spell's damage to its own player, and a shout's
    push knocks it down (its ragdoll streams). A teammate's kill is a real death (spectate, respawn), and every chat
    names the killer ("was killed by"). Decided with the user: no XP or kill counts, not a crime, and nobody turns
    on the attacker (the victim's game runs no NPC reaction for its player). Kill moves on players work. Found on the
    way and fixed: a player revived after a kill-move death floated (its ragdoll left in the physics world;
    `death.md`, "The ragdoll"), and spectating could drop into first person (third person only now). Harness:
    camera, body pose and ragdoll motion readouts, `coop_test.py look` (`testing.md`). Open: melee stagger is not sent
    (as for host NPCs); a copy's ragdoll (any actor's) no longer fights its owner's frames (steered), and the engine keeps a lying
    copy on it as its owner (its spins and snaps fixed; `physics-sync.md`, "A copy's ragdoll, steered"); knock-start
    pops fixed (never placed where its owner lies, eased off its ragdoll). Left for after 1.0 (`docs/bugs.md`): a
    copy getting up on its own timing, a little shaking while it lies, and a revived player knocked down stays getting up.

18. **Dialogue judged for each player** (built 2026-10-28, `.worktrees/party-scripts-2`;
    [shared-dialogue.md](design/shared-dialogue.md), "The rule"): the host's engine judges every topic of a step for
    each player's character too (`JudgedFor`: conditions on PlayerRef, a PlayerRef parameter, `GetPC*`, intimidate and
    bribe rewritten for the character), and each game's menu shows its own list; the host's game says the picker's
    line. Copies now carry their player's skills from the spawn, and its added spells with its perks (GameTests
    `perk_flows.cpp`; `HasSpell` reads them through `Actor::VisitSpells` 38781). Tiers 1-2 pass; Skyrim `dialogue-line-judged-for-guest`
    (fails as expected with the hook off), `dialogue-options-are-each-players` (Lucan's claw), and the dialogue tag.
    Known gap: a guest with an empty own list leaves. Harness fixes found on the way: the after-scenario body check
    read a first-person player's first-person model (`dialogue-refused-start`, `-picker-gets-speech-experience`
    failed on it, dev too), and a spawned NPC recruited by a scenario's "Follow me" pick stayed in the follower slot
    once deleted (every `follower-*` fixture after it failed): `remove_host_npc` dismisses it first.

19. **Crafted potions and poisons** (built 2026-10-28, `.worktrees/crafted-potions`;
    [crafted-potions.md](design/crafted-potions.md)): a player-made potion or poison is named in the session by its
    recipe (one id for the same effects on every game), each game making its own form from the recipe
    (`AddPotion`/`AddPoison`); the recipes reach each game before the first message naming one (`TransportService` and
    `HostNetwork` send the book's new entries first) and are kept in the character record. Brewed potions reach the
    brewer's copies, chests, drops and the host's pickup; a coated weapon keeps its poison. Found on the way: the alchemy
    menu adds its potion with `TESObjectREFR::sub_19688`, which no hook saw (no copy got a brewed potion); hooked for
    actors. Tiers 1-2 (ClientTests, HostAuthorityTests, TPTests, GameTests `crafted_potion_flows.cpp`); Skyrim
    `guest-brewed-potion-reaches-host`, `guest-poison-coating-reaches-host` and ten item regressions pass;
    `crafted-potion-kept-across-rejoin` passes its potion steps and fails the known cover-plate difference after a rejoin.
20. **Adaptive copy playback** (built 2026-10-28 on dev; [copy-clock.md](design/copy-clock.md)): copies
    trail their owner by what the line needs instead of a fixed 300 ms (one game-wide clock steered toward the worst
    lateness of the last 3 s, plus each copy's own send interval), movement, actions and ragdoll on that one time;
    late snapshots carry a copy on for up to one interval. Players, mounts and fights are sent 30/s, other actors 10/s.
    Tiers 1-2 (ClientTests `copy_clock_tests.cpp`, GameTests `copy_playback_flows.cpp`); Skyrim: 16 of 19
    movement/combat/knockdown/death/travel scenarios pass, the other three known and in `docs/bugs.md`; the guest's copy
    of the host's player shows about 140 ms behind (was 300).
21. **v1 checks: permanent abilities and live menus** (2026-10-28). An ability's values reach a rejoined
    player's copy (the spawn carries values held off their base; non-pool values are set as the base, not capped by
    damage). Live chest and barter menus close when their target goes, dies or leaves (`MenuAutoClose`, Skyrim Souls'
    rule), and an open chest's list follows another game's change (`SendInventoryUpdateMessage`). ClientTests
    `menu_auto_close_tests.cpp`, GameTests `perk_flows.cpp` (rejoin); Skyrim `ability-stays-on-copy` and the
    `coop_menus.py` live-menu checks pass; a chest lid's pose after an interrupted open is fixed (News, 2026-11-18).

Not in 1.0: a player list with kick and a settings screen (players join by Steam invite), the trade menu, mod support
(RaceMenu adapter, animation mods, mod sync). Party map/compass markers are implemented on dev as recorded above.

## Product status and remaining delivery work

The earlier "Product pieces not started" list was obsolete. Steam lobbies/invites and the P2P transport,
game/build/plugin-order checks, main-menu returning-character joins, host save transfer and character co-save
records are implemented. The join UI shows progress/refusals/end reasons; ordinary leave, kick, host exit and
exhausted reconnects now clean up and return to the main menu. Difficulty follows the host's actual setting.
See `docs/design/join-flow.md`, `Code/client/Ui/README.md` and the integration review above for evidence and limits.

Animation action snapshots now copy and replace their owned strings/vectors under one mutex; refused death
actions preserve newer actions and body resets. Fast concurrency/restoration tests and native checks pass.
The original reload corruption remains open (v1); the historical werewolf crash is not reproduced (2026-10-29,
`docs/bugs.md`). See the [stability report](v1-session-items-2026-10-28.md#concurrent-animation-snapshots).

- **V1 delivery (2026-10-31):** one zip for friends, extracted into the Skyrim folder, then Play: SKSE and Address
  Library bundled, our launcher as `SkyrimSELauncher.exe` (version check, INIs, enables the start esp, starts SKSE);
  `package_check.py` boots it from only the extracted zip (PASS). [install.md](install.md).
  The host's New Game skips Helgen with Alternate Start 4.2.6a and the USSEP 4.3.9c it needs, bundled (the user,
  2026-10-31; about 250 MB zip; results in mod-compatibility.md). The PDB stays home: `build/package/<package>-symbols/` keeps the DLL and PDB, and
  `scripts/re/symbolize_crash.py --build <commit>` decodes a friend's crash log with them.
- **V1 delivery:** `scripts/package.sh` builds the player zip (dev mode, no harness; plugin, UI, SkyrimCoopStart.esp,
  SkyrimCoopLauncher.exe, `docs/install.md`, `docs/playtest.md`), byte-identical from the same build (2026-10-29).
  `scripts/package_check.py` installs and boots it from a clean folder and prefix (PASS). The README now points to install.md; its old server/launcher
  description below still needs cleaning up.
- **V1 first-time join (planned 2026-10-29):** a friend the host's save does not know accepts the invite at the main
  menu, a new game starts by itself, Quick Start skips the intro, and naming the character joins the host with it
  ([join-flow.md](design/join-flow.md#first-time-join-planned-2026-10-29-v1)). Today such a friend is refused with
  "start or load a game of your own". The vanilla intro is out of v1 (`docs/ideas.md`).
- **V1 acceptance:** the agreed upstream fixes/features and library checks in [v1-playtest-todo.md](v1-playtest-todo.md),
  the unresolved host-reload freeze with its save/load gate (`save-load-gate`: five save/load ways with the door's
  autosave, a follower kept through them, a minute of fighting after each; 3/3 on vanilla 2026-10-29, the mod profiles next), and ordinary packaged-build Steam, quest/save/rejoin and 3–4-player runs.
- **2026-10-30 (v1-party-owns):**
  - A house bought is the party's on every game: a cell's owner is now a world-state row (`FormChangeRows`).
  - An NPC's relationship with the player is the party's standing (`JudgedRules`).
  - Party gold: a spend is told in every game's chat, and every game's HUD shows the change.
  - The scenario runner boots the duo again when a scenario breaks the baseline.
  - The harness's `gait` readout catches sliding actors.
  - The ownership scenarios (`--tag ownership`).
  - Next: party items ([party-items.md](design/party-items.md)).
- **Later scope:** automatic mod/file syncing or self-updating, a launcher if needed for those features,
  player-list/kick/settings screens and the other items explicitly outside 1.0 above. V1 entry uses Steam invites
  and Skyrim's own save/character flow; no additional Host Game or character-library screen is required.

## Infrastructure (done)

- Linux cross-build with clang-cl (`4571bcbe`), one-file rebuilds in ~3 s (`71858be5`).
- Two-game scenarios off-screen, through real input (`088d78da`); harness out of player builds (`ea087fb2`).
- Hot reload into running games (`d09fdd1f`).
- Crash and hang tooling: CrashLogger reports, breadcrumbs, heap history, probes, repro, vanilla
  harness (`f4ff0467`); lock-order checker (`261be264`); desync checker (`8e14e17d`).
- RE references: `docs/re/ENGINE_REFERENCE.md`, CommonLibSSE-NG and SKSE plugin sources, IDA database.
