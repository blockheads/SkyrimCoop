# Known bugs (after v1.0.0)

Bugs we have seen and chose to ship with in 1.0. Each entry: what a player sees, the evidence, where to start, and the
test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then the fix, then
delete the entry and say where the evidence went. Unfinished work and unexplained behaviour stay in
`docs/progress.md` ("Loose ends"); ideas in `docs/ideas.md`. The fixes and regression evidence for feature 14's
visible actor bugs are in [small-bugs.md](design/small-bugs.md).

## The party is the Dragonborn

- **A spell the story gave while a guest was away does not reach it** (2026-10-27, dragonborn). A joining character's
  spells are its record's (`PlayerRecord::Spells`), so Beast Form given by C03 while that player was offline is
  missing on its game until the story gives it again; factions and words come with the host's save.
  - Start: keep the story's spells beside the host's records (`HostRecords`), or mark them in the save.
  - Test: a `join_flows.cpp` case (the fake's `AddsSpell` while the guest is at the main menu).
- **A joining guest gets the host's soul-bought unlocks** (2026-10-27, dragonborn). A word's unlocked flag is the
  word form's (one per save), so the host's Magic menu unlocks arrive with its save; the guest's own come back from
  its record (`PlayerRecord::Words`).
  - Test: `join_flows.cpp`, the host spending a soul before the guest joins.

## Quest rewards and copies

- **A player's copy does not carry its earned player perks** (2026-10-27, quest-rewards). After a shared
  Agent of Mara grant, both actual players report `HasPerk CC799` rank 1, while the guest's copy on the host
  reports rank 0. Character rewards are on both owners; a host script or condition querying the copy's perk
  list can still read false. Actor values (including the carry-weight reward) do follow the owner.
  - Evidence: `.worktrees/quest-rewards/build/reward-copy-perk-probe.log`; the real T02 turn-ins assert both
    owners' perk lists. Reproduce by a host `Actor.AddPerk(CC799)` on the guest's copy, then the console's
    `player.hasperk CC799` on each game and `<copy>.hasperk CC799` on the host.
  - Start: the copy's perk list from its owner, including initial spawn/rejoin and later changes; do not grant
    a reward only to the copy or turn all personal perks into party shares.
- **A quest alias filled with a created actor stays host-only** (2026-10-27, quest-rewards staging). The guest
  has the actor's copy, but its quest role can retain the old fill. The turn-in tests therefore use placed givers.
  - Reproduce: spawn an Inge clone, `quest_force_alias` into BardsCollegeLute alias 17, compare quest snapshots.
  - Start: HostEngine::ReadAliasFill marks temporary references HostOnly unless the loose-object creation path
    knows them; actor fills need the session actor mapping instead. A Skyrim scenario is needed until the fake
    models this engine distinction.
- **Quest start/stop events are not completion events on a guest** (2026-10-27, quest-rewards). Real turn-ins
  consistently emit the guest's native stage, stage-item-done and QuestStatus completion events, but a T02
  turn-in recorded host stop 1 / guest stop 0. A plugin using only TESQuestStartStopEvent can miss that stop
  although the guest journal is completed. Experience's DLL is not installed/tested here.
  - Test/readout: `quest-perk-turn-in-guest`, `quest_fragments`' start/stop and completion counters.
  - Start: `GuestEngine::ApplyQuest` and its direct running/starting flag writes; preserve the rule that only
    the host rolls and runs world quests.

## Physics and ragdolls

- **A knocked-down copy gets up on its own timing** (2026-10-26, friendly-fire). The copy's get-up is its own
  engine's knock update, not its owner's: it gets up up to 1.6 s early or (once) 7 s late, and now and then plays a
  different get-up, facing 40-110 degrees away from its owner's for up to 2 s.
  - Seen: `knockdown-smoke` (Lydia knocked down three times between the players) fails about one run in five; its
    `compare_report` lists each get-up's time on both games and the facing gaps.
  - Related: Skyrim pops a getting-up skeleton onto its reference for one frame at knock state 6, owners too (up to
    80 units). A copy getting up apart from its owner shows the pop at another time, and the smoke counts it as the
    copy's own.
  - Start: make the copy get up when its owner does (its owner's knock state 5 replayed, or the copy's knock update
    held until then), with a GameTests flow in which the fake copy would get up first. Notes:
    `docs/design/physics-sync.md`, "A copy's ragdoll, steered" (open items).
- **`death-spectate-guest` failed once after the ragdoll steering work** (2026-10-26). The host's copy of the dead
  guest lay 16 units from the guest's body, 3 bones up to 17 units off their ragdoll bodies. It passed at 16:49 that
  day, before the physics changes; not run again or looked into.
  - Start: run it 3 times; if it fails, compare a `RagdollTrace` of the guest's body and its copy (as
    `knockdown-lies-as-host` does). Suspects: steering (`BodyBridge::Drive`), and copies never placed from a lying point
    (`CharacterService::RunRemoteUpdates`, `Movement::Lying`).
- **A knocked-down copy's bones still shake a little** (2026-10-26). `knockdown-lies-as-host` fails its jitter check:
  a bone turned back 4 times within a second, 8 flickers, a bone 35 units off its body (before the steering work: 9
  to 12 turn-backs a second). `kill-move-kills-guest` fails on the same check.
  - Start: the trace's per-bone series (`ragdoll.json` under `build/coop-test/traces/`), steering gain
    (`BodyPrediction::kSteerSeconds`), and whether the shaking bones are the ones the copy's ragdoll driver powers.
- **A revived player knocked down later stays getting up** (knock state 6), and its copies never go down. On dev
  before friendly fire too. `docs/design/death.md`, "The ragdoll" (open).
- **The host's jail release reported a lock inversion** (2026-10-26, shared-party-bounty): GuestPhysics
  `FreePlaced`/`FreeBody` during the release, symbolized in that worktree's `build/crime-re/guard-jail/`.
- **A physics carry check fails in the full gate** (seen by shared-party-bounty, 2026-10-26, "baseline physics carry
  failure"), and pickpocketing ended once with the QA ladles at another pose on the guest. Not traced.
  - Measured 2026-10-26 (dropped-items): `physics_flows.cpp:147` ("A guest's player carries the plate and lets go"),
    the host's plate 75 units from the guest's where 20 is allowed, in about 3 of 8 gate-style runs (8 shards at once)
    with dev's binary and with dropped-items' alike; 5 of 5 alone. Under `net_check.sh` both profiles also fail
    `physics_flows.cpp:279` (a knocked-down NPC's copy's bones) and abort in `:354` (a guest's player's ragdoll), on dev
    too.
  - Start: the flow under CPU load (the gate's shards); its timing assumptions against `BodyPlayback`'s 100 ms.
- **A guest's ragdoll can rest before its copies are dead** (2026-10-27, found merging into dev). Under
  `scripts/net_check.sh`'s first profile (delay 50, jitter 30, loss 5%), `physics_flows.cpp`'s "A guest's player dies
  and ragdolls" ends with every copy's bones on the owner's pose but the copies alive (`IsActorDead` false on both
  other games). It fails the same on dev with only the purse added; it used to crash the fake instead
  (`FakeEngine::Drive` took a body frame for a copy not yet made, which the real `BodyBridge` treats as not loaded
  here, fixed the same day). Without the bad line it passes.
  - Start: the order of the death state and the ragdoll's frames on the copy's game (`ActorValueService`'s death
    replay waits for the owner's action; `BodyService` drives the bones meanwhile).
  - Test: that flow under `COOP_NET="delay=50,jitter=30,loss=5,seed=1"`.

## Combat

- **Melee stagger is not sent.** A power bash on a player (or a host NPC's copy) hurts but does not stagger it; only
  the hit's damage reaches the victim's game. `docs/design/friendly-fire.md`, "Melee stagger".
