# Small visible bugs (v1.0.0 feature 14)

Built in `.worktrees/small-bugs` from dev `241bb0f2`, 2026-10-27. No Papyrus, party rewards or progression changes.

## NPC resurrection

A copy needs a living graph as well as an alive flag. `ActorValueService` resumes a successfully resurrected NPC
copy. Between frames, `SkyrimBridge` restores `kShouldAnimGraphUpdate | kCanSpeak` and sends the living graph
`IdleForceDefaultState`. Native resurrection loads its 3D asynchronously: a refused event is retried on later main
loop frames (at most 120). The accepted reset leaves the graph's death state, so draws and attacks work again.
The queued operation resolves an engine handle, never a retained actor pointer. Resetting 3D alone did not fix it.

`Actor::Resurrect` (37321, signature checked in 1.6.1170) performs `IdleForceDefaultState` before changing life state.
That native action must pass the copy's action gate and stay local on an owner too: relaying its dying state made
the guest treat the resurrection itself as a newer death. `HookResurrect` holds the existing forced-action flag
through the native call, restoring its previous value afterward (`DeathActions::WithLocalReset`).

Fast regression: `resurrection_flows.cpp`, `[resurrect-animation]`, plus the three `[resurrect-events] cases. The fake
retains death's graph and stopped updates on a copy after resurrection, and models the native reset action while
still dying. An owner's native reset runs locally; a copy also needs its living graph reset after loading.
Skyrim: `death-host-npc` uses a mortal bandit, kills and resurrects it, then checks its existing copy's actual graph
swing and drawn weapon. The test removes its other weapons after resurrection so the AI cannot choose a bow.
Player recovery still remakes player copies; that policy is unchanged.

## Joining beside the host

A brought character has no saved place in the host's world. After loading the host's save and applying the character,
`JoinService` waits for `PlaceBesideHost` before reconnecting and announcing it. Kept characters retain their saved
place. `CheckedPlacement::Beside` tries 18 bearings 120 units from the save's player position, checking floor support,
slope, height difference, headroom, capsule clearance at two heights and a clear path from the anchor. Missing
physics or an incomplete ray query never counts as clear. No clear spot within 20 seconds gives `placement_failed`.

The harness and joining use the same engine ray implementation (`Havok/Raycast`); `bhkPickData` and the collidable's
filter field are typed, with offsets checked against CommonLibSSE-NG. The harness's existing checked staging stays.

Fast regressions: `[checked-placement]` and `[join-placement]`, including joining held until there is room.
Skyrim: `guest-brings-character-into-hosts-world` checks separation and settling after arrival.

## A copy's level

`CharacterService` applies `NotifyPlayerLevel` to the matching `PlayerComponent`'s existing copy, setting its NPC
base level and change flag `0x2` (the write made by TESActorBaseData::SetLevel, 14385). A fresh copy takes the latest
roster level too. The roster remains `SessionService`'s responsibility. No new level message is needed.

Fast regression: `[copy-level]`, levels rising and falling in both directions without replacing the actor.
Skyrim: `party-shows-levels` checks each existing copy's base level as well as the roster. The harness reads the
field directly; comparing the whole base-data section would also compare unrelated flags and attributes.

## Instant FX Persist visuals

A fire-and-forget effect with zero duration can start and end before the queued active-effect read. Its start now
sends the existing flash message even with FX Persist. The receiving flash path also permits FX Persist: changing
only the sender had left the receiver rejecting the flash. `HitVisualsRules` keeps constant abilities, concentration
and lasting FX Persist effects out of the instant-flash rule, and still respects No Hit Effect. A copy plays the
shader and art with lifetime zero, allowing their normal passes and fades to finish.

Fast regressions: `[instant-visuals]` in ClientTests and GameTests; the fake effect is already gone before its frame.
Skyrim: `copy-flashes-instant-effects` casts a shrine blessing at Vilkas and checks CureDiseaseEffect's HealMysticFXS
on both games and its ending. `copy-flashes-one-shot-effects` retains Gift of Charity's non-persisting regression.
The start hook (34094) remains `void(ActiveEffect*)`; its calling convention and body were checked in IDA.

## Looks on the existing actor

`RequestRespawn` with appearance data now forwards those looks in `NotifyRespawn`; an empty appearance still asks
for replacement after player recovery. The host keeps the new appearance and live state for later spawns.
`CharacterService` queues the newest looks by session id and applies them on `MainLoopTasks`, between frames. It
loads the existing temporary NPC base with `FormState::Load`, sets weight, switches the actor's runtime race when
needed or resets its 3D, and replaces the FaceGen tint component (removing it when tints are empty).

A queued update retries while a save/load holds FormState. Leaving cancels pending work, and a lifetime token keeps
a queued callback from reaching a destroyed service. A graph rebuild invalidates the cached variable descriptor:
keeping a humanoid layout on a werewolf graph corrupted its variables and crashed the next graph update. The actor,
base and form id stay the same. Reapplying PlayerFaction here was redundant and changed its rank; it is left to the
existing faction sync.

Fast regression: `[copy-looks]` checks both owners, race changes, one update at RaceMenu close, a delayed save,
empty tints, graph descriptor invalidation and unchanged actor ids. The fake runs its own main-loop queue before
each game's jobs and refuses appearance loads from a job. The appearance notification has a binary round trip,
and HostAuthorityTests checks its forwarded fields and later spawn record.
Skyrim: `guest-changes-looks-in-session` compares the base save sections and painted tints through RaceMenu and a
vampire change, keeping the form id; `client-beast-form-stays-client` checks werewolf and human transitions.

## Validation

Final targeted Skyrim run: all nine feature scenarios passed in shuffled order (seed `20261028`):
`death-host-npc`, `guest-brings-character-into-hosts-world`, `party-shows-levels`, `copy-flashes-instant-effects`,
`copy-flashes-one-shot-effects`, `late-join-client`, `equip-host-npc`, `guest-changes-looks-in-session` and
`client-beast-form-stays-client`. NPC resurrection also passed three consecutive runs once its accepted reset was
waited for (7-8 main-loop frames). The additional `death-spectate-host` regression failed its wolf-target staging
in the shuffle, then passed alone, including player recovery.

- `scripts/net_check.sh '[copies],[join-placement],[resurrect-animation],[resurrect-events],[instant-visuals]'`:
  15 cases / 254 assertions pass at both 50 ms delay, 30 ms jitter, 5% loss and 120 ms, 80 ms jitter, 10% loss.
- `scripts/check.sh`: ClientTests 52 cases / 555 assertions, HostAuthorityTests 161 / 1765 and TPTests 14 / 227
  pass; UI fixtures and plugin build pass. All 137 GameTests run; 136 pass and the existing plate-carry case fails
  at `physics_flows.cpp:147` (75 units behind, about 20 allowed).
- Unchanged dev `241bb0f2`, archived and built separately, reproduces that same plate failure using the current
  gate's shard order: 75.00787 versus 20.16393. No physics code or test was changed. The separate baseline's earlier
  runs also showed its known intermittent ragdoll assertion at `physics_flows.cpp:403`.
- Python syntax and `git diff --check` pass. Newly added source helpers have affected-scenario mappings.

Retained worktree artifacts: `build/feature-verified-live.log`, `build/feature-death-regression.log`,
`build/npc-ready-live.log`, `build/feature-verified-net.log`, `build/feature-verified-check.log` and
`build/baseline-dev/matched-gate.log` (original source and shard logs beside it). Earlier red tests and diagnostic
runs are retained under `build/`; the shared Skyrim animation traces are under `build/coop-test/traces/`.
