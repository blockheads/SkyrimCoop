# Progress

Where SkyrimCoop stands: what works, what is open. Update it when a system or known failure changes; what changed
and when is in the commits and pull requests. Later ideas are in `docs/ideas.md`, open bugs in `docs/bugs.md`, engine
findings in `docs/re/`, the words (host, guest, owner, copy, player) in `docs/design/glossary.md`.

Design notes (`docs/design`, `docs/research`, `docs/reports`) were removed on 2026-11-26. A pointer to one, here or in a
code comment, reads from history: `git show b7b43a3bd:docs/design/<name>.md`.

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

## Sync by system

Each verified system has a two-game scenario (`scripts/coop_test.py scenario <name>`).

| System | Status | Scenarios |
|---|---|---|
| Session, join, leave, kick, host rejoin | works | `session-roster`, `late-join-client`, `host-rejoins`, `host-kicks-client` |
| Crafted or truncated messages (2026-11-26) | dropped: a non-player's messages other than a join go unread, every count is bounded (`Serialization::ReadCount`), a short read drops the message, snappy output is capped at 16 MB | tier 1 `[encoding.decoder]`, tier 2 `stranger_flows.cpp` |
| One field list per message (2026-11-27) | a message or struct lists its fields once (`Fields`, `Wire/Archive.h`), both archives walk them, `==` defaulted; the hand-written serializers left: differential (`ActionEvent` and the messages carrying it), quantizers, versioned or validated reads | tier 1 `[encoding.fields]`: every opcode against bytes recorded from the old serializers |
| Movement (players, host NPCs) | works | `steady-state`, `move-host-npc` |
| Cell changes (`coc`, doors, fast travel) | works | `teleport-keeps-copies`, `party-changes-cells`, `party-uses-door`, `party-fast-travels` |
| Equipment and hands | works | `equip-*`, `repair-equipment-drift`, `hands-survive-reload-*` |
| Draw / sheathe | works | `draw-*`, `quick-sheathe-*` |
| Sneak, swimming, diving | works | `sneak-*`, `dive-*` |
| Casting | works: copies replay in step with their graph and cast for free | `cast-*`, `input-mouse-cast`, `firebolt-*`, `flames-*` |
| Health | works | `health-*` |
| NPC death and removal | works: a copy dies with its owner's death action | `remove-host-npc`, `death-host-npc` |
| A player going down (`docs/design/death.md`) | works: a real death in a session (`OnDeath` and `TESDeathEvent` fire, the reload held), copies die by their owner's death action, spectating once dead, revived beside the party (abilities, hands, graph and camera back; 2026-10-26); after a wipe the dead host loads a save it chooses (nothing loads by itself, 2026-11-16) and each guest stands where it stood in it; each player's ragdoll streams from its own game to the others (2026-10-26); any player drags a dead player's body with a held E, the drag reaching every game (2026-11-27, `re/systems/physics.md`, "Dragging a corpse"; until its respawn) | `death-spectate-host`, `death-spectate-guest`, `death-wipe-host-loads`, `host-drags-dead-guest`, `guest-drags-dead-host`, GameTests `[death]`, `[corpse-drag]` |
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
| Time, weather | works; weather per region (2026-11-22: a guest holds the weather the host's game gives its own region, the host's where forced or shared, faded into; `docs/re/systems/weather.md`) | `guest-weather-own-region`, `host-time-reaches-client`, `guest-sets-time`, `host-wait-reaches-guest`, `shared-wait-host`/`-guest`, `shared-sleep-host`/`-guest`, `shared-wait-cancel`, `late-join-gets-calendar`, `party-fast-travels` (days passed), `weather-follows-host` |
| Party panel (RmlUi) | works alone; fails after other scenarios | `party-panel` |
| Player levels (party panel) | works | `party-shows-levels` |
| Real input (keyboard, mouse) | works | `input-keyboard-draw`, `input-mouse-cast` |
| Mounts | covered; ownership returns after dismount/travel/leave | `guest-rides-host-horse`, `host-rides-horse`, `guest-rides-across-border`, `mounted-guest-leaves`, `party-travel-mounted-guest` |
| AI package, dialogue, factions | relayed; individual coverage documented | `docs/design/copy-contract.md`, `docs/design/shared-dialogue.md` |
| Physics: objects and ragdolls | **2026-11-15: held to a vanilla baseline** (four collision trials, 40 vanilla rounds: 20 with no session, 20 with the plugin's physics hooks off; `physics-sync.md`, "One simulator per pile"): one game simulates each object (the mover's, what its objects knock too; copies push nothing; no relayed push; the host's own placement wins), others steer theirs onto it. A guest's push no longer jumps to the host's result (300 units to under 2 on the ladle line); 2026-11-16: the last two full runs pass 6 and 7 of the 8 `*-like-vanilla` trials (40 vanilla rounds each; in the second every trial that ran matched vanilla, the eighth timed out in staging): pushes and carries match from both walkers, the tavern pile in most runs; its misses are rests a few units apart and a snap now and then (`bugs.md`, "Physics against vanilla"). A rest gets its contacts before it sleeps, glides on, sleeps a still pile as one (Havok sleeps islands, not bodies), a rest being put is not the host's own move (the hopping ladle), the host's own placement drops a guest's rest, and moved bodies are asked about together. Before that: the host simulates, guests show it live (keyframed by its frames, sent when the guess goes wrong) and rest at the host's spots; a dead NPC's copy lies as the host's bone by bone. A guest's push goes to the host as its character's impulse, and its weapon's hit as the swing's (2026-11-13: the hit had snapped back; one at a body the host's frames move does nothing yet; a still copy no longer keeps the clutter beside it awake), its casts through its copy's, what it grabs follows its hands on the host and falls from where it let go; the guest shows its own until the host's rest, and gets the host's pose back for what its own physics moved alone. A guest gets frames only of bodies within 4096 units of its copy, rests of all (2026-10-25). A guest's playback is placed at its pose each frame and blends into frames it has early: no flicker, 0.1 units typical off the host's (2026-10-25, `FlightTrace` in the fireball scenarios). Since 2026-10-26 every game streams its own bodies (a guest's player's ragdoll, what it holds), and a living actor's copy is knocked down with it and lies as its ragdoll does. Not yet: a refused second grab, a dead body's pose at a cell load (`docs/design/physics-sync.md`) | `ragdoll-lies-as-host`, `knockdown-lies-as-host`, `guest-knockdown-lies-on-host`, `guest-bump-moves-host`, `guest-swing-moves-host`, `guest-throw-lands-on-host` |
| Crime / party bounty | host-authoritative | shared totals, guest alarms and fines from the party's purse; native coverage and jail/trespass-AI limits: `docs/design/shared-party-bounty.md` |
| Dropped items | one in the host's world with their extra data; pickups granted by the host (feature 6, 2026-10-26); a dead player keeps what it held, out of reach, and holds it again on respawn (2026-11-06, `re/systems/death-drops.md`) | `docs/design/dropped-items.md` |
| Followers | on dev: shared vanilla slots, member anchors, fallback and picker inventory; native coverage/limits in design | `docs/design/followers.md`, `follower-*` |
| World state (quests, globals, containers, mods' state) | not synced; what drifts and how often is measured (world-state sync steps 1-2: `save-diff`, `change-rate`, `docs/research/world-state-drift.md`); a form's save data round-trips in a running game (step 3, 2026-10-20: `engine/FormState.cpp`, `form-state-round-trips`; the copies' appearance uses it); quest walks 2026-10-20: `coop_test.py quest-walk` drives real quests (main, side, radiant) from a late save (`scripts/fetch_late_saves.py`) and shows the client never follows (`docs/research/world-state-drift.md`, "Quest walks"); step 4 built: globals and quests from the host (`HostWorldState`, `NotifyGlobals`, `NotifyQuests`; a guest applies with the engine's own calls, stage fragments skipped; scenarios `global-reaches-guest`, `late-join-gets-globals`, `quest-stage-reaches-guest`, `quest-completes-for-guest`, `quest-roles-match-host`, `late-join-gets-quests`), the main-quest walk matches 14 of 14; loose ends below. Step 5 slice 1 built 2026-10-21: a guest's world scripts get no activation or trigger events, the host's run them for its copy, and their doors, animations and spells reach the guest from the host (`Papyrus/WorldEvents.cpp`, `HostWorldState`'s `NotifyObjectChanges`, `MagicTarget.cpp`; `docs/re/systems/guest-world-events.md`; scenarios `sword-box-asks-client`, `statue-fires-at-client`, `ring-turns-for-client`, `shrine-blesses-client`). Step 6's enabled state built 2026-10-21: the host's references enabled or disabled reach the guests (`NotifyObjectChanges`; `sword-box-asks-client`, harness `ref_enabled`). Locks the same day: the host's lock changes go out as object changes, a guest's are proposals the host's game takes (`HostObjects::OnLockChange`; `lock-reaches-others`); the old lock relay is deleted. Both read on the host from the engine's change funnel (`Forms/FormChanges`, `HostEngine::TakeObjectChanges`; `docs/design/world-state-sync.md`, "As built"), built 2026-10-21 (scenarios `lock-reaches-others`, `guest-loads-propose-no-locks`, `sword-box-asks-client` pass); the `Enable`/`Disable` hooks are deleted. Containers slice 1 the same day: a container's item changes travel as its count now (`kItems` object changes from the host; a guest's own a `ContainerChangeRequest` the host's game takes), seen through the engine's `TESContainerChangedEvent` (`ContainerEvents`); `chest-take-reaches-others`; the unused `RequestObjectInventoryChanges`/`NotifyObjectInventoryChanges` deleted. World desync check for references (step 7) 2026-10-21: harness `world_state` (with each reference's change record, pose, physics, graph, effects), `coop_test.py world-diff`, scenario `world-changes-reach-guest` (also each change's object events). Reference sync reworked the same day from what it showed: rows for enabled, lock, open state, move, scale and owner (the engine's calls: swing, events, 3D, collision), the funnel also watching `RemoveChange` and repeat moves; every kind but created references matches in every side effect, items' container event included. The save-data fallback is built but off until mods (with it on, it hid the missing container event); flags with no row are logged once, `coop_test.py unsynced` (now: havok moved, activating children, animation). Open: created references, those rows, whether the guest's scripts should get object events at all, references the guest has not loaded (changes dropped). Destructibles 2026-11-27 (Bleak Falls Barrow's webs, playtest 2026-11-26): their damage was game-only extra data no row sent, so a guest got only the destroyed flag (whole model, blocking, its own swings refused); now a `kDestruction` row (health, the engine's own `DamageObject` on the guest), a guest's damage an `ObjectStateRequest` the host takes as damage only, a copy's swing damaging nothing (`docs/re/systems/destruction.md`; `arvel-web-cut-reaches-guest`, `guest-web-cut-reaches-host`). Rewritten 2026-10-20: save transfer at join, plain messages while playing (step 4: globals and quests), save data per form only for mods later; AI state travels on its own | `docs/design/world-state-sync.md`, `docs/re/systems/form-state.md` |
| A client's actions counting on the host ("who is the player") | spikes 1-5 and a script scan done; decided (`docs/design/party-model.md`): every party member is the player in the host's scripts, "the player" is the member whose action started the script, position questions without a cause ask about the party, story events judged for their member, messages to everyone. Rules 1-2 built (`engine/Papyrus/GuestScripts.cpp`; rule 2 never ran until 2026-10-20: `GetPlayer`'s name reads back from the engine's string pool as `getplayer`; engine names are now compared case-insensitively everywhere, `sync/core/EngineNames.h`, `docs/re/systems/engine-names.md`; scenarios `plate-counts-client`, `sword-box-asks-client`, `statue-fires-at-client`, `ring-turns-for-client`, `keyhole-asks-client`, `shrine-blesses-client`, `vilkas-counts-client-hits`); upstream's patched vanilla scripts and `SkyrimTogetherUtils` natives removed; rules 3-5 not built. The client's world scripts stop reacting to activations and triggers (world-state sync step 5, slice 1, 2026-10-21); other events still run on both games | `docs/research/player-events.md`, `scripts/spikes/` |

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
  - Tests: GameTests "A guest's player presses a held plate against a host NPC's copy"; UnitTests
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
      UnitTests "A body this game's player holds still is not at rest".
    - `host-sets-down-item` and `host-drops-item-stays` pass 2 of 2.

## Loose ends

Everything known to be unfinished, unverified or unexplained that is not a bug a player meets (those are in
`docs/bugs.md`). Close an entry by fixing it, proving it harmless, or moving it to `docs/ideas.md`; say where the
evidence went. Details are in the doc named. Triaged against dev on 2026-11-16: solved, outdated and news entries
removed (git history keeps them).

- **A dragged object blinked once in play (2026-11-01).** Not reproduced with `body_trace`; nothing traces a hold; a
  scenario that drags an object while both games trace it would catch a recurrence.
- **Replace the health faces before a Nexus release (2026-11-18).** `Code/plugin/Ui/assets/moods/*.png` are
  RollerCoaster Tycoon art, not ours and not GPL; swap in our own or CC0 faces.

### Mounts (2026-10-23)

- ~~A copy made while its owner rides starts off the horse~~: built 2026-11-17 ("A ride is a hold" above).

### Mods

- **EngineFixes and our `FormAllocate` hook** (2026-10-22): upstream rehooked it when EngineFixes replaced
  it, but only from its own launcher, so under SKSE the rehook never ran and is deleted. Whether
  EngineFixes' allocator patch overwrites our hook (the heap history and form allocation) is untested.

### World state

- **A copy's arrows and spells still damage destructibles in the other game** (2026-11-27,
  [destruction.md](re/systems/destruction.md)): only a copy's melee swing is skipped (`PlayerHands::CopySwings`). A
  copy's projectile or explosion meeting a web damages it in the game that shows it; on a guest that becomes a proposal
  (the host takes only damage, so at worst a web breaks a hit early). Not measured. Also: a guest loading a cell after
  the host broke something replays the stage's explosion there (a trap's blast, a web's puff).
- **A guest's Storm Call may not hurt host NPCs** (2026-11-24, [projectiles.md](re/systems/projectiles.md), inferred
  from our code, not measured): the host runs the NPC's storm script, which blames the guest's copy; if the bolt's
  `AddTarget` caster is that copy, `HookAddTarget` drops it on the host's own NPC. Needs a scenario reading the
  NPC's health.
- **Storm Call's created activators end apart** (2026-11-24, `storm-call-bolts-reach-guest`'s end comparison, logged
  under `own-checks`): its activators (`ACTI B79FF`, placed by `magicProjectileStormScript` and moved by `SetPosition`)
  are on the host only, or in another cell or pose on the guest, after the storm. Invisible markers; whether the
  script's moves and its `Delete` at the effect's end reach the guest's copies was not read.
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

- **Done 2026-11-26: actors are found by id in one lookup** (`core/ActorIndex`, kept by the registry's signals): the
  `Utils::` session/form id functions and the services' own scans of every entity use it (cleanup step 3).

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
  node). Not reproduced on fresh games (5 of 5). Likely the walk fixed 2026-11-27 (below, "UnequipAll's walk").

- **An NPC on the guest only** (2026-10-20): after play in Whiterun the guest showed an NPC at a door that the host's game did not; `desync` listed `Ulfberth War-Bear [client 000D15B0, sid -1]: copy in client with no owner in the other game`. Not looked into; the guest's own actors are the copy contract's.

### Engine threading and crashes

- **Closing the window crashed** (playtest 2026-11-26, fixed 2026-11-27): the X / Alt+F4 destroyed the window and,
  with `bAlwaysActive`, one more frame resized its swap chain; `WM_CLOSE` now quits as `qqq` does
  (`docs/re/systems/game-window.md`, `repro quit-close-window`).

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
- **Done 2026-11-27: the courier's menus offered "(Invisible Continues Linked to Self)"** (playtest 2026-11-27). His
  force greet is an Invisible Continue line: the host's engine says his deliveries itself (35285), but the host sent the
  start's list as the step and every menu showed it. The host sends no list while the said line continues
  (`IDialogueBridge::IsContinued`; [dialogue.md](re/systems/dialogue.md), "Invisible Continue lines"). GameTests "A
  courier's force greet continues on its own...", scenario `courier-force-greet-continues`. Open: a walk-away choice
  hidden by flag 0x100 (+0x4A) is not carried in party lists yet; no playtest met it.
- **Done 2026-11-27: a copy's rebuilt leveled base recorded as its reference's base change.** `RelevelActor`'s
  `SetBaseObject` recorded the temporary NPC's id (0x80); the next load of the reference's stored changes gave that
  NPC to the reference the cell made again and destroyed it there (19529, 12095), under the old reference or a held
  copy. Caught with the heap guard on 0x268-byte blocks; a temporary base is no longer recorded (`BaseChange.h`,
  UnitTests `[base-change]`, `leveled-copy-base.md`).
- **Done 2026-11-27: a quest object's holder shows its [P] tag** (the Golden Claw, the Dragonstone). The item menus
  name a stack by its list's `ExtraTextDisplayData` (`InventoryEntryData::GetName` 16018); the holder's quest-object
  list was never tagged, so only the other players' copies read "[P] ...". The host's arrival (or, for one held from
  the save, the party's items arriving) now tags it in place, never replacing it (`inventory-changes.md`, "A quest object's party tag"; GameTests "A guest picks up a quest object",
  scenario `golden-claw-party-tag`).
- **Done 2026-11-27: UnequipAll's walk read a node its unequip freed.** The engine's `UnequipAll` (38899) walks the
  inventory holding the next node, and an unequip that empties the first entry frees it (16067); our reconcile's reset
  of a copy's worn set ran it. Found by the heap quarantine in the six mount scenarios (the guest's 2026-11-27
  cell-attach crash's investigation); `EquipManager::UnequipAll` now reads the worn stacks first (`WornStacks.h`,
  UnitTests `[worn-stacks]`). That the night's cell-attach crash was its victim is inferred
  (`docs/re/crashes/2026-11-27-guest-cell-attach-null-virtual.md`).
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
  locks. Listed in `scripts/lock_order_known-1.7.104.txt`, not fixed.
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
    `HostArrivals::BringToParty`; tier 2 late-join flow, `BridgeTests` "a player joining away from the host").
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

- **2026-11-26: bug-finders.** Sanitized native tests and `FuzzMessages` (testing.md, "Sanitizers and fuzzing"); in
  debug and harness builds `World::Get()` logs a caller off the update and main thread (hooks queue through
  `World::Runner()` and read `World`'s static accessors; since 2026-11-27 no caller is left: the UI draws from a
  snapshot the main thread fills, Code/plugin/Ui/README.md). Under ASan many GameTests flows miss their real-time waits (slowness).

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
- `Projectile::LaunchData` (`engine/Projectiles/Projectile.h`) names `+0x50` `fYAngle`; CommonLibSSE-NG has a pointer there (`unk50`) and `desiredTarget` at `+0x58`. Projectile sync sends it as `YAngle`. Not checked in IDA.
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
   Validation: full gate PASS (142 GameTests, 57 UnitTests, 164 authority cases, encoding/UI/DLL);
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
   event on a player plays on each game's own player, on any other reference on the host only (UnitTests
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
    events runs (a copy's kill too); a party arriving together is one arrival. HostAuthorityTests, UnitTests, GameTests; Skyrim
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
    2026-11-27: the scratch saves a join or host load writes (`SkyrimCoopJoin`, `SkyrimCoopWipe`) are deleted once
    any load ends and on reaching the main menu: the main menu's Continue loads the newest save of any character and
    took them, leaving character id 0 and a Load list of other playthroughs' saves
    ([save-game.md](re/systems/save-game.md), "Continue and the Load list"; GameTests `[saves]`). A friend joining a
    loaded save without its character creates one, a save with it brings it back. In a game the Journal's Load lists
    only the loaded playthrough's saves (no other characters, no "Show all saves"; a guest in a session lists none),
    and a console load of another playthrough's save is refused (`engine/SaveListHooks.cpp`, `SaveList`;
    save-game.md, "Only this playthrough's saves"; scenario `load-lists-only-playthrough`). The party load now sets the
    checkpoint's character with its modded flag too: keeping the scratch load's 0 left `{id, 0}` against saves filed
    `{id, 1}` (an empty list, and a save exposed to a catalogue reallocation, save-game.md). Open: a guest in the session when
    the host loads a save without its character keeps the character it plays (`OnPartyReload` brings it).
17. **The party's purse** (decided and built 2026-10-27, merged into dev;
    [party-purse.md](design/party-purse.md)): every player carries one purse, the host's player's gold. Any
    player's gold change (loot, trade, training, a fine, a reward on a guest's copy) reaches the host as before
    (`RequestInventoryChanges`), which keeps it in its player's gold (`HostPartyItems`) and sends the purse to every game
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
    actors. Tiers 1-2 (UnitTests, HostAuthorityTests, TPTests, GameTests `crafted_potion_flows.cpp`); Skyrim
    `guest-brewed-potion-reaches-host`, `guest-poison-coating-reaches-host` and ten item regressions pass;
    `crafted-potion-kept-across-rejoin` passes its potion steps and fails the known cover-plate difference after a rejoin.
20. **Adaptive copy playback** (built 2026-10-28 on dev; [copy-clock.md](design/copy-clock.md)): copies
    trail their owner by what the line needs instead of a fixed 300 ms (one game-wide clock steered toward the worst
    lateness of the last 3 s, plus each copy's own send interval), movement, actions and ragdoll on that one time;
    late snapshots carry a copy on for up to one interval. Players, mounts and fights are sent 30/s, other actors 10/s.
    Tiers 1-2 (UnitTests `copy_clock_tests.cpp`, GameTests `copy_playback_flows.cpp`); Skyrim: 16 of 19
    movement/combat/knockdown/death/travel scenarios pass, the other three known and in `docs/bugs.md`; the guest's copy
    of the host's player shows about 140 ms behind (was 300).
21. **v1 checks: permanent abilities and live menus** (2026-10-28). An ability's values reach a rejoined
    player's copy (the spawn carries values held off their base; non-pool values are set as the base, not capped by
    damage). Live chest and barter menus close when their target goes, dies or leaves (`MenuAutoClose`, Skyrim Souls'
    rule), and an open chest's list follows another game's change (`SendInventoryUpdateMessage`). UnitTests
    `menu_auto_close_tests.cpp`, GameTests `perk_flows.cpp` (rejoin); Skyrim `ability-stays-on-copy` and the
    `coop_menus.py` live-menu checks pass; a chest lid's pose after an interrupted open is fixed (News, 2026-11-18).

Not in 1.0: a player list with kick and a settings screen (players join by Steam invite), the trade menu, mod support
(RaceMenu adapter, animation mods, mod sync). Party map/compass markers are implemented on dev as recorded above.
