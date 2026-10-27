# Quest rewards

Feature 3, 2026-10-27. Each player present when the party earns a character reward receives its input on its own
player. Items remain the picker's; gold is the party's purse (`party-purse.md`). The rewards are party calls, as the Dragonborn's words and souls are
(`dragonborn.md`, "The party's calls"): the native call itself goes to every game.

```text
host: picked line / quest fragment / book script
  Game.IncrementSkill(By), Game.AdvanceSkill, Game.AddPerkPoints, player.AddPerk / ModActorValue
    -> GuestScripts' native boundary: PartyNatives::Scope, before PlayerNatives can send a global to just the picker
    -> PartyRules::RouteOf: the party's? (its cause, story and source)
    -> on the host's player: the original runs; on a guest's character: skipped, run on the host's player instead
    -> RoutedCallRequest -> HostPlayerNatives -> NotifyRoutedCall -> every guest
    -> PartyNatives::Run: the same native, tagged mirrored, on that game's own player

  player.AddItem / RemoveItem
    -> unchanged rule 2: the picker or its copy -> inventory sync -> that player's game only
```

```text
if not hosting, not in session, mirrored, wrong native or not a party player: run normally
read native arguments, never skill experience after a gain
if a body/effect/equipment script or a body's quest: run normally
if AdvanceSkill and a player's action without a story tag: run normally (the persuade's Speech)
otherwise:
  relay the call as it is (its arguments are the native's input), its player each game's own
  skip the original on a guest's copy: its owner supplies its values and perks
  every game's native takes the reward, once, with that game's own perks and mods
```

## The engine paths and the rule

Searched CommonLibSSE-NG and [Papyrus Extender](https://github.com/powerof3/PapyrusExtenderSSE) first; the existing
local checkouts are under `reference/`. Vanilla scripts below are in `build/re/scripts/Source/Scripts`.
No new native hooks or addresses are needed for grants: GuestScripts already intercepts NativeFunctionBase::Call
(104651), whose frame holds the call; the natives' engine functions are rows of the progression table (party-calls.md), tested in ClientTests.

| Input | Engine path (1.6.1170) | Shared |
|---|---|---|
| `Game.AdvanceSkill(skill, amount)` | Papyrus 55449 -> PlayerCharacter::AddSkillExperience 40488 -> PlayerSkills::AdvanceSkill 41561, with use modifier | A story's, including a stage the picker's script set; an action's without a story tag remains personal |
| `Game.IncrementSkill`, `IncrementSkillBy` | Papyrus 55476 / 55477 -> IncrementSkillFromPapyrus 55616 -> PlayerSkills::IncrementSkill 41562 | Explicit script grants, including a dialogue fragment before SetStage (Amren, TIF__00071051) and the Oghma's book script |
| `Game.AddPerkPoints` | Papyrus 55451, this game's PlayerCharacter perk point count | Explicit script grants; each game's native applies its own limit |
| player `Actor.AddPerk`, `RemovePerk` | Papyrus 54642 / 54645, actor virtual call; the player's own perk list | Lasting script grants and their reversals, including a Black Book's changed choice; not a body's effect or equipment |
| player `Actor.ModActorValue` | Papyrus 54721 -> permanent modifier (ActorValueOwner::ForceCurrent, mode 0) | Any value a script's permanent modifier changes (health, magicka, stamina, carry weight, dragon souls); positive and reversing deltas; not temporary/damage writes or a body's effect |
| `AddItem` | The picker's own player or its copy, existing inventory sync | Items personal; gold to the party's purse (`party-purse.md`) |

`ModAV` is a Papyrus wrapper around `ModActorValue`, so it reaches the same boundary. Perk and stat grants from
active magic effects, armor/weapons, scripts on the player, or quests its body started are personal. This prevents
an effect's permanent-underneath `ModAV` (such as PerkDarkPotencyScript) from becoming a party reward. No native
skill-use, book or training engine call is intercepted as a reward: they do not call these Papyrus functions.

The Black Books' reward scripts add/remove perks directly from their controller, not an active effect. The Oghma's
script runs from OnActivate or OnEquipped on its book: a book is not equipment for this rule. There is no script-name
or quest-id allowlist. An arbitrary mod using these APIs has the same cause/source rule; custom skill frameworks'
separate APIs are not adapted.

The call goes as the script made it: its arguments, integers and forms (mapped to the session and back), are the
native's input. A mirrored native is never sent again. The host does not also route a shared global through
PlayerNatives to the picker.

## The owner and its copy

The receiving game's own player must hold the skills, perk points, perks and permanent modifiers: modifying only
the host's copy would leave the actual character unchanged. A copy's current/permanent actor values follow its
owner's writes through ActorValueService; `quest-explicit-rewards-reach-party` checks a carry-weight reward made on
the guest's copy against the guest's own value. The copy is not a second recipient of experience.

Measured limitation: the player perk list is not carried to its copy today. After giving Agent of Mara to the
party, both owners' console HasPerk reports rank 1, the guest's copy on the host rank 0. That affects scripts or
conditions querying the copy, not receipt on the actual players. It is recorded in bugs.md with the native probe;
this feature does not turn personal perk builds into shared builds or pretend a copy holds its owner's list.

## Experience mods and quest events

Quest sync already uses `TESQuest::SetStageWithoutFragments` on a guest. The ordinary stage and completion
machinery runs; only SkyrimVM's fragment lookup is bypassed. The guest does not re-run the host's reward script.
The harness now listens to the real dispatchers for TESQuestStageEvent, TESQuestStageItemDoneEvent (the story
manager's stage-finished callback), TESQuestStartStopEvent and QuestStatus::Event (source 25196). It unregisters
before hot reload. The source's no-argument/pointer-returning signature was checked against CommonLib and the
1.6.1170 disassembly (RVA 0x3D8E00).

Both host and guest turn-ins of Finn's Lute, The Book of Love and The Golden Claw produced on the guest:

- one native stage event and stage-item-done event for the turn-in;
- one QuestStatus completion event;
- zero reward fragments.

A plugin listening to those stage/completion events sees its game's actual completed quest and can award its own
player's experience. This is evidence about the engine events, not a test with the closed Experience DLL (Nexus
[17751](https://www.nexusmods.com/skyrimspecialedition/mods/17751)) installed. A script/mod depending on running the
quest fragment on every game still gets no guest fragment. Start/stop notifications are not equivalent to a
completion: guest stop counts vary with the engine's stage/flag path (see captured counters); do not claim that a
listener only to TESQuestStartStopEvent sees every completion. A joining load restores historical state silently,
without live completion awards (`quest-apply.md`).

## Personal experience and the host's pick

Calibration found the old measured combat XP relay adding a shared reward again: a 0.75 native Alteration gain
became 2.25 on the host after the guest's own gains returned. The relay and both CharacterService XP handlers,
LastUsedCombatSkill, the 27244 multiplier override and the `SyncExperience` messages are removed. Skill use
remains personal (`xp-stays-personal`).

40488's hook is retained only to dirty a guest's character record (`PlayerProgressEvent`, no measured gain). Its
signature now forwards advance object (r9) and action (fifth argument). CommonLib and disassembly agree: 40488 at
RVA 0x736E20 forwards the action from entry rsp+0x28 (rsp+0x70 after its 0x48 allocation) to 41561.

The host's own begin fragment can run before NotifyDialoguePicked comes back. HostConversation::OnPickSent now
sets its picker to 0x14 and its speaker before allowing the native menu's pick. Previously it cleared the picker,
so an immediate persuade looked like a no-cause story call and shared its Speech. The fake now models that timing;
the new dialogue flow fails without the fix. The existing Skyrim persuade scenario tests both pickers.

## Joining and loading

Decision: no catch-up ledger. Skills, skill XP, perks and perk points already earned stay with the character in its
PlayerRecord. A player absent for a later grant does not acquire it on rejoining; a new character does not inherit
the host's skill build. Rewinding the host's world uses the character record belonging to that save.

Records now also carry permanent totals for health, magicka, stamina and carry weight (base plus permanent
modifier, excluding temporary buffs and damage), and carry weight's base. JoinBridge restores them as totals,
without replaying any award. This prevents the host save's permanent reward from replacing the guest's own amount.
The optional trailing field reads old records with no totals; their missing historical modifiers cannot be
reconstructed. GuestRecord becomes dirty on party notifications, with a five-second delay for queued native writes
to finish before the snapshot. Native skill progress still dirties the record independently.

`quest-rewards-kept-on-rejoin` gives the guest a different permanent total, grants the party a reward, waits for the
host's character record, disconnects it, grants another reward while it is away, then loads the host's world through
the real join flow. The guest keeps its earned amount and receives none of the absent-time grant.

## Tests and validation

- ClientTests: the reward natives' rows and routes, source/cause rules, mirrored calls, personal effects and item
  exclusions.
- HostAuthorityTests: the host's progression call reaches every guest once, with no echo; legacy measured XP is not
  relayed.
- Encoding: character permanent totals, and records predating totals and words.
- GameTests: four players, both quest pickers, per-game XP multipliers once, direct dialogue/book levels, points,
  perks and reversal, permanent stats, personal Speech/effects; early host dialogue fragments; character rejoin.
- Skyrim (`scripts/coop_rewards.py`): six real vanilla quest turn-ins, calibrated AdvanceSkill, direct book-style
  rewards and a stat on a copy, full save-transfer rejoin. Turn-ins use the real placed quest givers, native
  dialogue lists and keyboard choices. Only the pre-turn-in quest stage/aliases are staged. End fragments wait
  for the NPC's actual response to finish. Full quest snapshots, native events, once-only skill levels, and the
  picker's item changes and the party's gold are checked. A spawned actor alias is HostOnly today, so clones cannot be used for
  this journal assertion. Stopping Finn's Lute runs its reward-bearing shutdown stage; setup resets instead.

After the rebuild as the party's calls (2026-10-27): `scripts/check.sh` PASS (634 / 57 ClientTests, 1772 / 163
HostAuthorityTests, 227 / 14 encoding cases, 1628 / 142 GameTests, UI fixtures, DLL); `scripts/net_check.sh
'[dragonborn],[rewards],[records]'` PASS in both profiles (172 assertions / 10 flows each); Skyrim: the nine reward
scenarios, the six Dragonborn ones, the picker's Speech and training, personal combat XP, the shrine and two cutscene
regressions, 21/21.

Final validation (before the rebuild):

- `scripts/check.sh`: PASS, 834 assertions / 57 ClientTests, 1859 / 164 HostAuthorityTests, 239 / 14 encoding
  cases, 1628 / 142 GameTests, UI fixtures and DLL build (`build/reward-check-complete.log`). The known intermittent
  plate-carry gate failure reproduced in earlier runs and passed in the final gate; no physics change was made.
- `scripts/net_check.sh '[rewards],[records]'`: PASS in both profiles, 111 assertions / 5 flows each
  (`build/reward-net-final.log`).
- Eight reward scenarios three times shuffled, seed 20261027: 24/24 PASS, 298.5 s, including all six real turn-ins
  (`build/reward-repeated.log`).
- Full host-world rejoin with earned/absent-time permanent rewards: PASS (`build/reward-regressions.log`).
- Speech picker, training, shrine, host beast form, both dragon kills/souls/art and personal combat XP: 6/6 PASS
  (`build/reward-regressions-final.log`).
- Python compilation, affected-source tag audit (1374 files) and `git diff --check`: PASS. New reward tags and
  missing merged-feature family mappings were added; no broad scenario suite was run.
- Duo stopped, zero Skyrim processes, reservation freed. No commits. Development used a separate worktree and an
  unchanged snapshot of the uncommitted Dragonborn prerequisite; `build/quest-rewards-feature.patch` isolates
  these changes from that snapshot.
