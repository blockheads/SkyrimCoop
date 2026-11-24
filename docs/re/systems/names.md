# Names in a session (2026-11-05)

Who shows what on which game, and what has been checked. Read in IDA 1.7.104; "played" is `names-in-every-case` on the
duo, "fast" is `GameTests [names]`.

## How the engine reads a name

- **`TESObjectREFR::GetDisplayFullName` (19781)** is the read behind what these show, each read in IDA: crosshair
  (`PlayerCharacter::UpdateCrosshairs`, 40621), activate text (`TESNPC::GetActivateText`, 24716), enemy health
  (`EnemyHealth::Update`, 51671), barter (`BarterMenu`, 50956), a dialogue path (`MenuTopicManager`, 35282). Our dialogue menu
  passes it too (`DialogueBridge::RealShowMenu`). Subtitles' path was not read; that they follow is inferred. So one
  correct reference name should cover them.
- It returns the reference's `ExtraTextDisplayData` (`GetDisplayName`, 12768) if there is one, else the base's `TESFullName`
  (`TESForm::GetFormName`, 14720).
- `ExtraTextDisplayData` is either a custom name (alias `-2`, pooled text) or a quest alias's message
  (`GenerateName`, 12775). It saves with the reference (12773, 12774).
- **The name is not always in the save data.** `TESActorBase::SaveGame` (24387) writes `TESFullName` only with change flag
  `0x20`, and `RaceSexMenu::ChangeName` (52415) is what marks it for the player. A character nobody named this
  session has no `0x20`.
- **Quest aliases name references:** `BGSRefAlias::InitializeRefAlias` (24011) sets the name through
  `ExtraDataList::SetQuestDisplayName` (11655), `BGSRefAlias::ClearRefAlias` (24012) removes it. Only the game running the quest
  does this: the host, for NPCs.

## How a name travels

One rule (since 2026-11-18, one-simulator.md piece 2): a copy's name is its record's `kName` part, declared by the
actor's owner (`ActorStateService`) and applied one way: overlaid on the reference (`ExtraTextDisplayData`, never the
shared base) only if it reads differently, compared as the engine's pool does ([engine names](engine-names.md)).

1. **Its owner reads it after each frame** (`GetDisplayFullName` 19781 of each actor it owns) and declares it when it
   differs from the record's. Whatever renamed it is caught: the race menu, a loaded save, a script's `SetDisplayName`
   (SKSE: `ExtraTextDisplayData` past the alias hooks) or `SetName` on a player's base, none of which raise an event.
2. **At spawn** the record's parts ride along, the name among them (read at the announcement), and the host keeps the
   newest one for a joiner. A player's copy has an unnamed base when its character has no change flag `0x20`
   (measured on the duo: flags `0x3000C40`; saved with `0x20`, the bytes hold `Prisoner`), so the name comes from here,
   not the snapshot.
3. **The party's lists** follow the same part: when the host keeps a new name part of a player's character
   (`HostRecords::OnKept`), `HostAuthority::NamePlayer` updates its list and sends `NotifyPlayerName`; each game updates
   its list (`SessionService`: party panel, markers, chat, join, leave and death notices, spectating). A name of
   nothing printable is refused, so a list keeps the last good name. (Was `PlayerNameRequest`, sent only when the race
   menu closed or a save loaded.)
4. **The host's quests rename an actor:** hooks on 24011 and 24012 raise `ActorRenamedEvent` (with the name as the call
   left it) when the display name actually changed. An actor the host owns: its own part says it. One another game owns
   (a horse a guest rides): `NotifyActorName` to that owner alone, which names its actor; its part carries that on. A
   guest's own quest naming a copy stays on that guest until the owner's next declared name.
5. **A guest's kept character** (`GuestRecord`) is saving, not sync: it reaches the host at most every few seconds, at once after
   the race menu closes and at once when its player's name part changes (`PlayerRenamedEvent`), or a save right after a
   rename would keep the old name and the load bring it back (seen in `names-in-every-case`: the race menu's name box
   sets the base's name before the menu closes, and the lists follow it at once).

## Checked

- Copies of each other's player at join, party travel, rejoin, the host's load, a guest's death: played. A guest on another
  guest: fast (no third game on the duo).
- A rename in the race menu: guest played, host and guest fast. Lists and copy both follow, and a guest joining after gets
  the new name (the host's stored spawn): fast.
- A vanilla alias name on an NPC (`dunBlindCliffQST` alias Petra forced on Hulda, 1A66E): played, also for a guest rejoining
  after. Aliased and cleared, during a reference's load, and on a guest-ridden horse: fast.
- Leveled NPCs (Bandit Chief on the Bandit Outlaw face): `GameTests [npc-choice]`; `leveled-spawned-npc-is-hosts` and
  `leveled-placed-npc-is-hosts` keep it on the duo. Named NPCs in the Bannered Mare agreed, played.
- Same name twice: nothing keys on a name. Rosters and markers key by player id, records by player key. The party panel's and
  chat's colour is by name (`PlayerColors::ForName`), so two players with one name share a colour; they are also
  indistinguishable by text, so left as is. Fast.
- Beyond ASCII: bytes go through unchanged; `ChatText` keeps UTF-8 whole; the map's label sets `text`, not `htmlText`. A name holding RML markup in the
  party panel's and chat's `{{ }}` bindings was not tried. `Björn`-style name played (the harness's `char` sends bytes, so it reached the
  game as `BjÃ¶rn`, and was the same on both games). A control-only name is refused.
- Empty, read not played: the join flow's creator only counts a non-empty name as naming (`CreationStart`); `GetLocalPlayerName`
  falls back on "Some dragon boi" for a nameless base while its copy would show "". A host's list keeps the last good name.
- Chat lines after a rename: `HostAuthorityTests`, not played (the chat line wouldn't open with Enter on that off-screen
  duo; the party panel read null too, and what it lists is `GetPlayers`, fast).
- `/bug`: `By` comes from `PlayerNameOf`, as chat.

## Not covered

- A mod's `SetDisplayName` or `SetName` at run time: copies and lists follow. Played: `script-renames-reach-party`
  (the host's SKSE `SetDisplayName` on an NPC, the guest's `Form.SetName` on its player's base) fails on dev
  `f7a72494` (the guest's copy kept "Wolf") and passes here; fast: GameTests "A script's rename with no event...".

## A stores-text alias's fill empties a custom name (2026-11-23, bug 21)

Read in IDA (1.7.104) unless marked measured; the branch journal `docs/re/journal/1.7.104/agent-horse-mount-guest.tsv`.

```
TESQuest::ForceRefIntoAlias (25052)
  ClearRefAlias on the old ref, then BGSRefAlias::InitializeRefAlias (24011) on the new one
    names it only with no ExtraTextDisplayData (or this quest's at another instance): a copy's custom name stays
  alias flag 0x100 (stores text): TESQuestInstanceData::StoreAliasText (23894)
    then TESQuest::ClearAliasNameCaches (25111): every ref alias with a display name (ALDN, or flag 0x4000)
      -> its reference's ExtraTextDisplayData::ClearCachedName (12769): +0x10 := the pool's empty string
ExtraTextDisplayData::GetDisplayName (12768): cache empty -> GenerateName (12775)
  a message name: the message's text, <Alias> tokens from the quest instance (23897, 25074): back as it was
  a custom name (no message, no quest, instance -2): the cache itself, now empty -> ""
```

- Stables (68D73), from Skyrim.esm (its QUST record): the purchase forces the horse into `PlayersHorse` (alias 40,
  flags 0x4102: stores and uses text, name message `PlayersHorseName` 9DE90). On the host the horse's name is that message's: cleared and regenerated.
- Measured (duo, `guest-mounts-bought-horse`, harness `name_data`): the guest's copy took its owner's name part first
  ("Prisoner's Horse", our custom name: instance -2, length 16), then the host's Stables state forced `PlayersHorse`
  onto it here (`GuestEngine::ApplyFills`) 30 ms later. Its text pointer then pointed at the empty string's pool entry
  (reference count saturated, 0x7FFF; the entry's layout read in `BSStringPool::LookupOrInsert` 69201), message, quest, instance and length unchanged. Its owner declared no new name
  (the host's never changed), so it stayed "".
- `CrosshairPickData::CanPickRef` (26130, read for dialogue.md) refuses a reference with no display name: the
  guest's crosshair ignored the horse (measured: the camera's ray hit it 158 units away) and it could not get on
  (`party-buys-horse-host`'s "the crosshair is on nothing").
- Built: `HookClearCachedName` (12769, `AliasNames.cpp`) keeps a custom name (`DisplayNames::CacheClears`). Vanilla
  only loses a custom name there too (an SKSE `SetDisplayName` on a reference a text-storing quest names); a message
  name is cleared as before. GameTests "A bought horse keeps its name on the guest when the stable's alias takes it".
