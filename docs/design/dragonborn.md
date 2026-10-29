# The party is the Dragonborn

v1.0 feature 11 (`party-model.md`, "One rule"): what makes "the player" the Dragonborn, or changes who the player
is, happens to every player, whoever's action caused it. Built 2026-10-27; rebuilt the same day as the party's calls
(below), which quest rewards (feature 3) use too.

```text
the host: a script calls a native that changes a player's progression (TeachWord, ModActorValue, AddToFaction...)
  on "the player" (the cause's character, or the host's own), and it is the party's (PartyRules::RouteOf)
  -> it runs there (or, on a guest's character, on the host's own player instead or as well)
  -> RoutedCallRequest -> every guest (NotifyRoutedCall): the same native on that game's own player
```

## What is shared, and what stays a player's

| What | Shared | Stays the player's |
|---|---|---|
| Words of power | learned or unlocked by a script (word walls, the Greybeards, Paarthurnax: `TeachWord`, `UnlockWord`), whoever's action ran it | a word unlocked with a soul in the Magic menu: the spender's; the console's |
| Dragon souls | every soul gained: each player's own value goes up by the same amount | spending: each player's own souls (decided by the user, 2026-10-27: "souls are per player once gained") |
| Factions | a change the story makes to a player: Harbinger, guild ranks, `PlayerMarriedFaction`, a spouse's house, the werewolf blood's | a player's own moment: its beast form's `PlayerWerewolfFaction` |
| Spells | one the story adds or removes: Beast Form and the Companions' immunity, a mod's power given at its quest's start | a player's own moment: its beast form's abilities, a standing stone, a shrine |
| Relationships | an NPC's relationship with a guest's character is with the player (0x7): the party's, as ownership is | |

**The story** is `PartyRules::Whose`'s (`cutscenes.md`, "Whose moment"): no player's action behind the script, or a
stage that action set. A body's script is that player's moment, never the party's (`BodyScripts::IsBody`): an
effect's, its equipment's, a script on the player, or a quest its body started (its beast form:
`WerewolfChangeEffectScript` starts `PlayerWerewolfQuest`, whose stage 1 adds the werewolf factions and abilities).

Titles and housecarls need nothing more: a Thane is the hold's favour quest (stages and globals, the host's world,
`FavorJarlsMakeFriends`), a title's rank is a faction, and the housecarl is that quest's alias, an NPC of the
host's (following a player is feature 7).

## The party's calls

Every one of these reaches "the player" through a Papyrus native a script calls, so the party's share is that call,
relayed: each game runs the same native on its own player (`PartyNatives::Scope`, the cutscenes' relay). Its HUD,
events and mods' listeners fire there as the engine's own; nothing is hooked per kind and there is no payload per
kind. A mod's scripts reach the same natives.

```text
which natives: the engine functions their code calls (NativeReach's table, party-calls.md), the natives:
  Game.TeachWord, UnlockWord, IncrementSkill(By), AddPerkPoints     any script's but a body's
  Game.AdvanceSkill                                                   the story's (a persuade's is its picker's)
  Actor.AddPerk, RemovePerk, ModActorValue (souls, a stat)            any script's but a body's
  Actor.AddToFaction, RemoveFromFaction, SetFactionRank,              the story's (a lever's is its puller's);
        AddSpell, RemoveSpell                                         on a guest's character it keeps them too
on whom: a global's player is the game's own; a method's object must be a player (0x14 or a guest's character)
where (PartyRules::RouteOf, the host only, never a mirrored call):
  on the host's player         the original runs; every guest runs it on its own player
  on a guest's character       the host's player runs it (queued, mirrored); every guest too; the original
                               runs on the character only for factions and spells (the host's conditions read
                               them there); values, perks and skills are its owner's to send
```

Not covered: what no script does (the Magic menu's soul unlock, the console: each player's own), and an SKSE
plugin's own native writing these without a script (none in vanilla; a mod's native could be added by name, or
classified from its code as `NativeReach` does for shows).

The relationship is the one write not made on the player: an NPC's relationship with a guest's character is read
and set as with the player's base (0x7), the party's, as ownership is (`GuestScripts`' hooks on BGSRelationship's
set 24075 and read 24084).

The join keeps the engine's own writes (IDA 1.6.1170): a word's unlocked flag is the form's (0x10000, set by 14640:
PlayerCharacter::UnlockWord 40630, Game.UnlockWord 55501), and DragonSouls' value sink (38505, ActorValueUpdateSinks
[133]) shows "Dragon soul absorbed" and sends DragonSoulsGained for any gain: silenced while a joining character's
own count is set (`DragonSouls::ScopedSilence`).

## A soul, traced

```text
the guest kills a dragon (the host owns it; the killer is the guest's copy: kill credit, friendly-fire.md)
the host: dragonActorSCRIPT.OnDeath(killer = the copy) -> its stack runs for the guest (rule 2)
  -> deadAndWaiting: getDistance(player) is the guest's copy (a stored player, rule 2)
  -> MQKillDragonScript.DeathSequence: AbsorbActor = Game.GetPlayer() = the copy
  -> AbsorbActor.ModActorValue("DragonSouls", 1): a progression call on a guest's character, the party's
     -> not run on the copy (its owner's game supplies its values)
     -> the host's player: ModActorValue("DragonSouls", 1), queued (its sink: the HUD and the event)
     -> every guest's player the same, the killer's too
```

No kill is faked: each game's own player gains its soul through the engine's own native.

## What every game shows

The dragon's death is the world's event, so every player sees it, wherever the kill came from (decided by the user,
2026-10-27: "yes everyone sees them"):

```text
a death engine event (OnDeath / OnDying) on an NPC, no native behind it -> the story (ScriptTags IsWorldDeath)
a native whose code shows something on a reference (NativeReach Call::kShow: ApplyArtObject 22769,
  ApplyEffectShader 19872, their stops 41396 / 41395, a sub-graph event 63372)
  -> every game runs it (PartyRules::ShareOf(kShow): here only when mirrored), on its own copy of the reference
"the player" in a story's call (the cause's character, or 0x14) -> each game's own player (LocalForm::EachPlayer,
  sent as 0x14): the absorb's streams run from the dragon to each player
```

A relayed call is queued as a method or global call (`GlobalCall::QueueCall`). The VM takes an object argument
only as its parameter's exact type (its check, 141461D00: "Type mismatch for argument"). An `Actor` passed to an
`ObjectReference` parameter fails, and the call never runs. A compiled script casts first. So each object argument
goes as the type its parameter declares (`ObjectTypeInfo::GetFunction`, then `IFunction::GetParam`). Until that was
fixed, every mirrored `VisualEffect.Play` and `EffectShader.Play` on an actor was dropped on the receiving game,
though its log said it ran.

Not shown elsewhere: the death's sounds (`Sound.Play` returns an instance id that no other game has). The souls'
HUD comes from each game's own sink.

## Pseudocode

```text
PartyNatives::Scope, around every native call in a session:
  progress = NativeReach::CallOf(native) is kProgress (party-calls.md: the router); none -> its other rows
  call = the native's call; player = its object, or 0x14 for a global
  target = 0x14 -> this player | a guest's character (host) -> guest character | else none
  route = RouteOf(progress, target, cause, story, BodyScripts::IsBody(caller script), hosting)
  if route.Relay:
      MarkEachPlayer(call, player)                 # sent as 0x14: each game's own
      if route.ThisPlayer: Run(OnThisPlayer(call)) # the host's player, mirrored (not sent again)
      if not route.Original: skip the native
      RoutedCallEvent(call) -> CutsceneService -> RoutedCallRequest -> HostPlayerNatives -> NotifyRoutedCall
each guest: PartyNatives::Run(call): GlobalCall::QueueCall, mirrored -> the native on its own player
```

## Joining and rejoining

- **Words:** a joining guest loads the host's save: the words it knows and the unlocks in it are the host's (the
  party's). Its record (`PlayerRecord::Words`) adds the words it unlocked with its own souls, and the shouts of every
  word it knows are added (`JoinBridge::ApplyPlayer`): the record's base brings only its own shouts.
- **Souls:** each character's own, in its record (`Values`, actor value 133, its current value), set silently at
  join: no HUD, no `DragonSoulsGained`.
- **Titles and marriage:** factions come from the host's save (the record keeps no `Factions` section,
  `join-flow.md`), relationships with 0x7 too.
- **Not carried:** a spell the story gave while a guest was away (its record's spells are its own); a
  host's soul-bought unlock reaches a joining guest with the save (the flag is the word's, one per save). Both in
  `docs/bugs.md`.

## For feature 3 (quest rewards)

Built on the same calls: [quest-rewards.md](quest-rewards.md). Its natives' engine functions are rows of the progression table (party-calls.md); vampirism
(feature 8, [vampirism.md](vampirism.md)) added `Actor.SetRace`'s row; its quest is each player's own
([system-quests.md](system-quests.md)).

## Tests

- Tier 1: the progression rows read from code (`code_reach_tests.cpp`), `RouteOf`, `OnThisPlayer`, `MarkEachPlayer` and `ShareOf(kShow)` (`ClientTests`,
  `party_tests.cpp`, `quest_reward_tests.cpp`), a death as the story (`script_tags_tests.cpp`), a show's natives
  (`code_reach_tests.cpp`); the relay of the host's progression call (`HostAuthorityTests`, `relay_tests.cpp`); a
  record's words and souls (`encoding.cpp`).
- Tier 2: `party_share_flows.cpp` (`GameTests`; the fake runs Scope's route and each native's write,
  `FakeEngine::TakeProgress`): a word a script teaches for any player, a soul on the guest's copy, a spend that
  stays, a story's faction and spell on a copy, a lever's and a body quest's that stay, a dragon's dissolve on every
  game; `join_flows.cpp`: a rejoin's souls and words.
- Skyrim: `words-reach-party` (was `words-reach-client`: the client's word wall reaches the host),
  `host-word-reaches-guest`, `dragon-soul-reaches-party` (a dragon each player kills; the client holds every art the
  host shows, an absorb stream on its own player aimed at its copy of the dragon: `reference_effects`),
  `title-reaches-party` (Harbinger and the werewolf blood from the story), `marriage-is-the-partys`,
  `host-beast-form-stays-host`.
- Hot reload: a dragon whose death sequence was still waiting when the games reloaded absorbs again when its
  copy unloads (a second soul). Only reloads do this. The scenario refuses to start while a dragon's script
  waits (restart the duo).
