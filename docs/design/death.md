# Death: spectate, then respawn beside the party

In a session a player really dies, as in vanilla: `OnDeath` and `TESDeathEvent` fire for mods, its body falls in the
engine's own death ragdoll. Vanilla's reload is held instead; the dead player watches a standing one, and the host
brings it back beside the party once the fight is over. If everyone is dead, the party loads the host's last save.

```text
a player's real death (its game)
        |
        v
its game: controls held, the host told (PlayerDeathRequest, one generation per fall)
        |                                   |
        |                                   v
        |                          dying -> dead: spectate the first standing player
        |                          "Spectating Lydia"; arrows / clicks cycle
        v
host (HostDeath): anyone standing? ---- no ----> WIPE: the host's previous save to every game,
        | yes                                    loaded everywhere, the party rejoins
        v
a standing player fighting? (its combat, or a host NPC's against it)
        | yes: no timer        | no: 10 s, started over by any fight
        v                      v
                        NotifyPlayerRespawn: beside the standing player with the lowest session id
                                   |
                                   v
its game: follows there (the tether's move), revives (PlayerCharacter::Revive), controls and camera back;
          the other games make its copy again, alive
```

```text
on_player_death(final):                       # TESDeathEvent: dying, then dead
    if not down: go_down(dying = not final)
    elif final and dying: dying = false; spectate_first_standing()

go_down(dying):
    down = true; generation += 1; hold_death_controls()
    send PlayerDeathRequest(character, generation, down)
    if not dying: spectate_first_standing()

host on PlayerDeathRequest / AI state / join / leave:
    if everyone known is down: wipe(); return
    if a standing player fights: cancel the timer
    elif someone is down and no timer: timer = now + 10 s   # game time: a pause stops it
host on timer: NotifyPlayerRespawn to each downed player, its fall's generation, beside the anchor

on_respawn(generation): if this fall's: follow_to(beside the anchor)
on_arrival_there: revive(); stand_up(); copies_made_again(); send PlayerDeathRequest(up)
```

## Where it runs

```text
Games/Skyrim     PlayerCharacter::Revive, HoldDeathReload; PlayerCamera::Spectate, LeaveBleedout;
                 PartyNatives::HoldDeathControls (beside the scripts' control holds)
every game       DeathService: this game's player down, spectating, respawn; the death screen (UiService, death.rml)
                 CharacterService: the copies of a revived player made again (PlayerRevivedEvent)
host             HostDeath: the party's state, combat, the 10 s timer, the respawn, the wipe
                 HostServiceReload.cpp: the wipe's reload; JoinService (guest): the wipe's save, as a join's
messages         DeathMessages.h: PlayerDeathRequest, NotifyPartyDeath, NotifyPlayerRespawn, NotifyPartyReload,
                 NotifyPlayerPerished (the chat line)
```

- The owner decides when its player goes down; the host checks the sender owns that character. Each fall carries a
  generation, so a late or repeated message for an older fall changes nothing.
- Combat comes from the owners' AI state (`AiStateRequest`, watched by HostDeath): a standing player fighting, or a
  host NPC fighting a standing player now or since its fight started (it switches targets). A downed player's stale
  combat flag holds nothing. A player who just joined holds the timer until its state is known.
- Respawn is the host's: `NotifyPlayerRespawn` names the anchor and where it stands (`HostCharacters::CharacterPlace`),
  each downed player 100 units off it, a sixth of a turn apart. The player travels as a follower (its arrival never
  pulls the party); if the party moves on first, it follows to where they went.

## Real death

- **The reload held.** Vanilla's reload is a timer: `PlayerCharacter` 40748 counts `fPlayerDeathReloadTime` (371051)
  down while the player is dying or dead, then loads the last save. In a session the setting is raised to 1e9
  (`PlayerCharacter::HoldDeathReload`) and put back on leaving. Players from older test builds were saved essential;
  holding the reload clears it.
- **Revive** (`PlayerCharacter::Revive`). The engine has no un-die for the player: its reload rebuilds the whole
  world, the player's body with it. Revive does the player's part:

```text
Actor::Resurrect(false)                              37321  alive, health restored, timed effects gone
Actor::CastPermanentMagic(false, true, true, true)   38753  base, race, alias and every-actor abilities
Actor::CastPermanentMagic(true, false, false, false)        worn enchantments
Actor::ApplyPerksFromBase (vfunc 0x101)                     perks' abilities (no duplicates on a repeat)
addedSpells through the instant caster               34402  AddSpell abilities, diseases too
Actor::DoReset3D(true)                               40255  the skeleton out of the death ragdoll
boolFlags |= kShouldAnimGraphUpdate | kCanSpeak             the graph runs, it speaks
the hands again (UnEquip, Equip; EquipSpell)                PlayerCharacter::Resurrect 40491 replaced the process
PlayerCamera::LeaveBleedout                          50824  the death camera (bleedout state 11) back to the player
LatestAnimation cleared                                     a copy made now starts standing, not in its death
```

- **The hands.** `PlayerCharacter::Resurrect` (40491) replaces the player's process: what it held is still marked worn
  but no longer in hand. Revive takes it off and equips it again (equipping a worn item leaves a stale worn entry;
  equipping before `DoReset3D` loses it; a forced equip locks it).
- **The graph.** `KillImpl` clears `kShouldAnimGraphUpdate` (boolFlags bit 8; the holder's vfunc 0x0E, 37985, gates
  `UpdateAnimationGraphManager` on it) and the dead branch of `Character::UpdateAI` (37347) clears `kCanSpeak`
  (bit 13). A load sets them again, a revive does not: the graph stood still (no draw, swing or event; walking still
  moved the controller). `DoReset3D`, the graph reset a load runs (32883), a living graph state and a knockdown did not
  help.
- **Why not a load.** Loading only the player's saved state (`FormState`, saved alive, loaded once dead; one game,
  2026-10-26) brings back what a save holds (alive, health, effects, the camera's state) but not the body: it stays in
  its ragdoll with the graph stopped, unable to walk or draw. It would also bring back timed effects and undo what
  changed while the player lay dead.
- Re-casting the abilities takes the alias array's lock before the player's reference lock, as vanilla does whenever
  an actor's 3D loads (`Actor::Load3D`, `Set3D`, `SwitchRace` call 38753); a save load takes them the other way, never
  at the same time (`scripts/lock_order_known.txt`).

Untested: a spell in hand at death (Revive's `EquipSpell` branch), dying as a werewolf or while mounted.

## Copies

- A player's copy dies by its owner's death action (`AnimationSystem` → `KillWithDeathAction`), as NPC copies do:
  copies are no longer essential, and `Actor::Kill` refuses only this game's player.
- On a revive the other games make the copy again from the owner's live state (`RequestRespawn` with its action,
  graph and values; `CharacterService::OnPlayerRevived`). Resurrecting the copy instead (the death state sync, tried
  2026-10-26) left its graph as death left it: it refused every replayed action after (`moveStart`, `WeapEquip`,
  `attackStart`). An NPC copy the host resurrects likely does the same (not checked: `death-host-npc` checks only the
  dead flag).
- Player copies are in the player's faction (0xDB1), so enemies take them as the player's party.
- **Copies' values.** The host keeps each player's values as its owner sends them (`HostCharacters` watches the
  relayed value changes into the player's record); a spawn from the host's copy never reads its values, which can be
  older (a guest with 5000 magicka once came back with 100 after a copy was made again).
- The host's player's ragdoll streams to the guests (`physics-sync.md`, "Ragdolls"); a guest's falls on each game on
  its own.

## Spectating and the screen

```text
Spectating Lydia
Left/right arrow or click to change player
Party must be out of combat for 10 seconds to respawn
```

- Three plain lines in the chat's font (`death.rml`, `aol-text.rcss`), shown only while down and no menu blocks it.
  No standing copy loaded: "Waiting for a player to spectate"; while dying: "You have fallen."; a wipe: "The party has
  fallen. Loading the previous save...", or why it failed.
- The view (`PlayerCamera::Spectate`): the watched character from behind in third person (SetCameraTarget 50838,
  `TESCamera::SetState` 33026), free look (`ThirdPersonState` +0xD4/+0xDC). The first standing player with a loaded
  copy, in session id order; a left click or the right arrow the next, the others back (`UiInput`). Chosen again when
  the watched player falls, leaves, or its copy unloads or is made again; never an actor pointer kept.
- Spectating is this player's own moment (`cutscenes.md`): movement, attacks, activation, sneaking, jumping and POV are
  held beside the scripts' holds, and given back only as they were (a cutscene keeps its own). A script holding the
  player's movement has the camera: the choice is kept and shown once it gives it back.

## Wipe reload

```text
everyone known is down -> HostDeath: wiped (once); only the transfer runs, every request dropped
    -> HostServiceReload: the newest gameplay save of the host's character (BGSSaveLoadManager 35760's catalogue;
       not SkyrimCoop's join or wipe saves) with its co-save
    -> NotifyPartyReload, then the save, to every guest (and any game still joining), as a join's
    -> each guest loads it (JoinService), its connection closed
    -> once none is left, the host loads it as "SkyrimCoopWipe", its rules made again on the same listener
    -> the guests rejoin as the characters the save keeps, each where it stood at that save
```

- Loading rolls back the world and the characters (`join-flow.md`). The guests' joins that come during the reload wait
  and are answered by the new rules. No save, or a load that fails: every player is told why, and nobody is revived in
  place.
- **Where each guest stands.** The loaded save's player is the host's, so a guest would arrive on the host's spot and
  the two would push each other along. The host keeps each guest's worldspace, cell and position when it saves
  (`HostRecords::OnSaving`, from `HostCharacters::CharacterPlace`), and `JoinBridge::ApplyPlayer` puts a kept
  character there. A character brought from the guest's own game has no place and arrives where the save's player
  stands.

## Decisions

| Rule | Decision |
|---|---|
| Death | A real death in a session; the reload held. Timed effects are lost, permanent ones come back. |
| Whole party down | Load the host's previous gameplay save; the party rolls back together. |
| Early revive | None: healing or a teammate cannot bring a dead player back. |
| Combat | The host decides, from every standing player's combat and host NPCs fighting a standing player. |
| Recovery | After 10 continuous seconds (game time) without such a fight, each dead player respawns alive beside a standing player, wherever the party is. A fight starts the 10 s over. |
| Camera | Its own falling body until it is dead, then the first standing player; arrows and clicks cycle. |
| Travel | The party's travel takes a dead player along, still dead and spectating. |

## Tests

- GameTests `[death]` (`death_flows.cpp`, the fake engine's real death: dying frames, then dead, the reload held): a
  real death (copies die with it, spectating once dead, no reload), the chat line, a guest dead through combat then
  respawned beside the host, healing reviving nobody, a standing guest's fight holding the timer, spectating a
  replaced copy, respawn after travel, a cutscene's controls kept through a respawn, a rebuilt copy keeping its owner's
  magicka, the wipe (world and characters rolled back, each guest where it stood) and a wipe without a save.
- HostAuthorityTests: a player going down reaches every game's chat.
- Encoding: every death message round trips.
- Scenarios: `death-spectate-host`, `death-spectate-guest` (a wolf kills the player; spectating, cycling, the survivor
  kills the wolf, respawn after 10 s with abilities, hands, draw, swing and sheathe; the host stays unseen by the wolf
  while it is staged, or it turns on the host), `death-wipe-checkpoint`.

## References inspected

| Source | Useful code |
|---|---|
| [Death Defiance System](https://github.com/NoahBoddie/death-defiance-system) ([Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/167571)) | `Hooks/Hook_SetLifeState.h`, `Hook_SendBleedoutEvent.h`: life state 37612 and bleedout event; essential/recovery handling. |
| [TrueHUD](https://github.com/ersh1/TrueHUD) | `src/HUDHandler.cpp`: `TESEnterBleedoutEvent` sink; `Widgets/InfoBarBase.cpp`: essential down display. |
| [Alternate Conversation Camera](https://github.com/NasiRawon/AlternateConversationCamera) ([Nexus](https://www.nexusmods.com/skyrimspecialedition/mods/21220)) | `Camera.cpp::SetCameraTarget`, `main.cpp`: switching between player and NPC, restoring target. |
| [Photo Mode](https://github.com/powerof3/PhotoMode) | `PhotoMode/Manager.cpp`: free-camera entry/exit and saved camera/controls. |
| [Acheron](https://github.com/KrisV-777/Acheron) | `src/Acheron/Defeat.cpp`: held bleedout, recovery and pacification; `Hooks/Hooks.cpp`: detection/combat suppression. |
| [SmoothCam](https://github.com/mwilsnd/SkyrimSE-SmoothCam) | `SmoothCam/source/camera.cpp`: reads the camera target actor; third-person camera lifecycle. |
| skymp (`reference/plugins/skymp`, `deathService.ts`) | Keeps its player alive instead: a deferred kill, `pushActorAway`, `GetUpBegin`. |

The earlier designs (an essential player bleeding out, then a knockdown held until recovery) were replaced by real
death on 2026-10-26: vanilla death already ragdolls the player and only its reload had to go.
