# Disintegrate and goo deaths

Why a player turned to ash froze its game, and what keeps it from happening (read in IDA on 1.7.104, 2026-11-22;
names and comments in the database, journal `agent-shout-kill-crash.tsv`).

## The bug

The host's Unrelenting Force killed a guest lying in its ragdoll: the guest's game froze (playtest, 2026-11-22).
Recreated by `shout-kills-lying-guest`. The ragdoll had nothing to do with it; the 10-health guest made the kill easy.

## The path

```
host's player shouts VoiceUnrelentingForce3 (13F3A) at the guest's copy
  Dragonborn.esm adds a fourth effect: DLC2VoiceDisintegrateFFAimed (0401FF2B, 40 damage),
  conditions: caster is the player, Dragonborn global 04020E99 == 1 (read from the plugin data)
friendly fire: the effects go to the guest's game (AddTargetRequest), applied to its own player
  the 40 damage kills it
  the effect's script DLC2AttachAshPileOnDeath25, OnDying (its source, read):
    SetCriticalStage(DisintegrateStart)
    TurnUnDisintegrate01FXS (8C1A9).Play      the glow players saw; CutsceneService relays it to the party
    AttachAshPile, waits
    shader Stop, SetAlpha(0.0, True) (bSetAlphaZero, true by default; the effect's VMAD keeps it)
    SetCriticalStage(DisintegrateEnd)
Papyrus SetCriticalStage (54747) -> Actor::SetCriticalStage (37615): stage stored at Actor+0xF0
  -> Actor::ApplyCriticalStage (37616): stage 2 (GooEnd) or 4 (DisintegrateEnd):
     middle process stopped, Set3D(actor, nullptr) (TESObjectREFR::Set3DLocked 12358), KillImmediate if not yet dead
the guest's player has no 3D:
  Main::Update waits for the frame's jobs (JobListManager::WaitFrameJobs 35409, job mode 1)
  a job thread waits in a wait job (WaitForJobTask 69516) for good: nothing crashes, the frame never ends
```

Read in IDA: the data's effect, the script's calls (source in the shared checkout's `build/re/scripts`), 54747, 37615,
37616, 12358, 35409, 69516, 54655, 54746. Measured on the duo: the 3D gone, the stalled main thread and job thread.
Inferred: which wait job, and why the player's update does not finish without its 3D (not read).

- Measured (duo, 2026-11-22): the guest's player lost its 3D 70 ms after the shader stopped; the hang watchdog's stacks
  10 s later had the main thread in 35409 and one job thread in a job tail-calling `WaitForJobTask`.
- Which wait job (inferred): `Job_Wait_Player_Update_1` (39049, stage 0 event 3 and stage 1 event 2) or
  `Job_Wait_For_Shadow_Culling` (39059); both tail-call 69516, so the stack does not say. Stage 1 event 2 is set at the
  end of every `PlayerCharacter::Update` path (`PlayerCharacter::UpdateCameraSignal` 40449, called by 40744 and 40748),
  so the first is the likely one. Which dispatch skips the player's update without its 3D was not read.
- Other callers of 37616 apply the stored stage again: the task queue (off the main thread), `Actor::FinishLoadGame`,
  `Actor::Resurrect` and `TESObjectREFR` 19742. The stage is written only by 37615 (Papyrus and the console).
- `Actor.AttachAshPile` (54655) queues `SkyrimScript::AttachAshPileFunctor` as a post-render call
  (`SkyrimVM::QueuePostRenderCall` 53955); the functor places the pile at the actor.
- `Actor.SetAlpha` (54746) writes the middle-high process's `alphaMult` (`+0x2C8`, `ActorProcess::SetAlpha`).
  Measured (duo, `alpha_trace`): the disintegrated guest's own player at 0 for the 6.5 s it lay dead, its copy on the
  host at 1, then 1 again at its revive. Read in IDA: `PlayerCharacter::Resurrect` sets up new middle-high process data
  (0x1406F0370 -> 0x140732DE0), which writes `alphaMult` 1.0. We leave the 0: it is the dead body on its own game,
  which spectates another player meanwhile.
- Vanilla never does this to the player: the disintegrate effects (this one, the Disintegrate and Deep Freeze perks')
  need the player as caster. Friendly fire makes another player the target.

## What we do

`Games/Skyrim/CriticalStage.cpp`, rule `CriticalStageRules.h` (ClientTests `[CriticalStage]`):

- `Actor::SetCriticalStage` (37615) refuses any stage but none for this game's player: it dies the normal way (real
  death, spectating, respawn), with its body.
- The refused GooEnd or DisintegrateEnd hides that body while it lies dead, on its own game, as the shooter's game
  already shows it (the copy dissolved by the relayed shader). Before (2026-11-22, the user on the duo): the victim's
  body showed again once the shader ended, lying and then getting up (it died knocked down), while the shooter saw
  nothing.

  ```
  37615 hook, this game's player, stage 2 or 4, the player dead now   (a later end: the revive went past, nothing)
    -> marked; at the frame's start (MainLoopTasks, main thread): its third-person root's kHidden set
  40476 hook (PlayerCharacter::ShowFirstPersonRoot): after the engine's switch, marked and dead -> hidden again;
    marked and alive (a revive, a wipe's load: LoadGame calls 40476) -> the mark dropped
  PlayerCharacter::Revive -> the mark dropped, 40476(camera first person): the roots as the camera has them
  ```

  - Read in IDA: `NiAVObject::Cull` (0x140EE0F30) returns at once for an object with `+0xF4` bit 0 (`kHidden`), so a
    hidden root's subtree is not drawn. `PlayerCharacter::ShowFirstPersonRoot` (40476; player, first person) writes
    opposite `kHidden` bits to the first- and third-person roots, only if the two are equal or the target root is
    not yet as wanted; off the main thread it queues itself. Callers: the camera states (`FirstPersonState::OnEnterState`
    50722, `ThirdPersonState::Update`, `BleedoutCameraState::Update`, ...), `PlayerCharacter::LoadGame` and
    `FinishLoadGame`. So the camera owns the third-person root's bit (a first-person player's is set), and the hide
    has to survive its switches; giving the bit back through 40476 restores the camera's state whatever it is.
  - Measured (duo, `shout-kills-lying-guest`, players in first person at boot): `mod.body_hidden` true while dead,
    as before the shout after the respawn; both games show it standing.
  - Inferred: that the frame's-start write and 40476's own (inline only when `ShouldUseTaskQueue` 39033 says no) do
    not race the culling's writes to the same word (71084 sets bit 26 during the cull, later in the frame). Not
    read: what keeps the shooter's copy unseen after the shader (seen on the duo, screenshots).
- `Actor.AttachAshPile` (54655) refuses this game's player: no pile left where it died (the world check saw one on the
  guest only before).
- The shader still plays for its 4 s, on the player and its copies: the death looks like one, and the body is gone
  on every game until the respawn.

GameTests `[friendly-fire]` "The host's Unrelenting Force with its disintegrate effect kills a guest": the fake runs the
ash pile script for a target that dies while the disintegrate effect lasts (1 s) and stalls a game whose player lost
its 3D; it failed with stage 4 and no 3D before the rule. "The disintegrate effect turns an NPC to ash only if it dies
while the effect lasts" keeps the fake to that effect and its lifetime. The scenario `shout-kills-lying-guest` checks
the push's hit, the shader on the victim, its death, its game running on and its opaque respawn.
