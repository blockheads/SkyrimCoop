# Names in a session (2026-11-05)

Who shows what on which game, and what has been checked. Read in IDA 1.7.104; "played" is `names-in-every-case` on the
duo, "fast" is `GameTests [names]`.

## How the engine reads a name

- **`TESObjectREFR::GetDisplayFullName` (19781)** is what every player-facing read uses: crosshair
  (`PlayerCharacter::UpdateCrosshairs`), activate text (`TESNPC::GetActivateText`), enemy health (`EnemyHealth::Update`),
  barter, subtitles and dialogue (`MenuTopicManager`). So one correct reference name covers them all.
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

## How a name reaches a copy

1. **A player's copy** is built on a created base from the owner's snapshot (`CreateCustomActor`). `NpcSnapshot::SaveFlags`
   always asks for `0x20` for a player, whatever it recorded. Measured before the fix: both duo bases had flags `0x3000C40`,
   the copies showed ""; saved with `0x20`, the bytes hold `Prisoner`. A later rename goes the same way (`ApplyActorLooks`).
2. **An NPC's copy** uses its own reference and base. The owner's announcement carries `DisplayName`, and
   `CharacterService::ApplyOwnerName` overlays it (`ExtraTextDisplayData`, on the reference, never on the shared base) only if
   the copy reads differently, compared as the engine's pool does ([engine names](engine-names.md)).
3. **A name that changes on an NPC with copies:** hooks on 24011 and 24012 raise `ActorRenamedEvent` when the display name
   actually changed; its owner sends `ActorNameRequest`, relayed from the owner only.
4. **The party's lists** (`HostPlayers`, `SessionService`: party panel, map and compass markers, chat, join, leave and death
   notices, spectating) hold the name from `AuthenticationRequest::Username`, taken after the join applied the
   character. `PlayerService` sends `PlayerNameRequest` when it differs: the race menu closing, or a save loaded (a host's
   load of another character keeps its session).

## Checked

- Copies of each other's player at join, party travel, rejoin, the host's load, a guest's death: played. A guest on another
  guest: fast (no third game on the duo).
- A rename in the race menu: guest played, host and guest fast. Lists and copy both follow.
- A vanilla alias name on an NPC (`dunBlindCliffQST` alias Petra forced on Hulda, 1A66E): played, also for a guest rejoining
  after. Aliased and cleared: fast.
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
- A mod that renames a player's base with `SetName`: no event; it arrives on the next race menu close or load.
