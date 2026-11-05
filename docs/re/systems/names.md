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

One rule: the host passes every name on, and each game applies it one way, `CharacterService::ApplyOwnerName`. It overlays the
name on the reference (`ExtraTextDisplayData`, never the shared base) only if it reads differently, compared as the
engine's pool does ([engine names](engine-names.md)).

1. **At spawn:** the owner's announcement carries `DisplayName`, a player's too, and the host passes it on. A player's
   copy has an unnamed base when its character has no change flag `0x20` (measured on the duo: flags `0x3000C40`, copies showed
   "" before this; saved with `0x20`, the bytes hold `Prisoner`), so the name comes from here, not the snapshot.
2. **A player renames, or the host loads another character:** that game sends `PlayerNameRequest` (`PlayerService`: the race
   menu closing, or a save loaded). The host updates its list and the stored spawn, and sends `NotifyPlayerName`; each
   game updates its list (`SessionService`: party panel, markers, chat, join, leave and death notices, spectating) and the copy.
3. **The host's game renames an NPC:** hooks on 24011 and 24012 raise `ActorRenamedEvent` when the display name actually changed,
   because the engine has no event for it. `HostService` sends `NotifyActorName`, whoever owns the actor, so a horse a guest
   rides needs nothing special. A guest's own quest naming a copy stays on that guest. A spawn still waiting on a loading
   reference takes the new name.
4. **A guest's kept character** (`GuestRecord`) is saving, not sync: it reaches the host at most every few seconds, at once after
   the race menu closes, or a save right after a rename would keep the old name and the load bring it back (seen in
   `names-in-every-case`).

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

- A mod's `SetDisplayName` at run time: `docs/bugs.md`. It arrives on the next spawn or refresh.
- A mod that renames a player's base with `SetName`: `docs/bugs.md`.
