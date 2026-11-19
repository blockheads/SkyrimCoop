# One simulator: every system on one sync rule (2026-11-18)

Status: **pieces 1 to 3 built (2026-11-18: the copy record; AI state, actor state, factions and name parts;
2026-11-19: items); the rest proposed.** Direction agreed 2026-11-18 (the four
decisions below). Physics
([physics-sync.md](physics-sync.md), "One simulator per pile") and the graph stream
([graph-input-stream.md](graph-input-stream.md)) already work this way; this puts the rest of the code on the same
rule, to end the edge-case bugs that come from two games deciding one thing or a copy missing what its owner did.

## The rule

```text
R1 one decider   each piece of state has one owner at a time; no other game's engine decides it
R2 state         other games get the owner's state, or its inputs in order; never an event whose
                 result depends on their own engine
R3 resync        a game that is late or has drifted asks the owner and is put right
R4 claims        a game that must act on another's thing claims it first; the host says who has it
R5 late names    state naming an actor that is not here yet applies when that actor appears
```

Other games still simulate: a followed object stays in their Havok, a copy's graph runs on their engine. They
decide nothing. The host does not win a disagreement; the **owner** does. The host decides who the owner is, and is
the owner when nobody else is (as `HostHands::m_holds`, `OnHostPlaced`, a silent mover's object).

Who owns what:

```text
an NPC, its items, its ragdoll, its detection       the host
a player's character, its items, its ragdoll        that player's game
an object                                           whoever moved it, until it rests; else the host
a pair (kill move, get-on)                          the game that claimed it first (piece 4)
the world: doors, locks, containers, quests,
  anything created                                  the host
ambient animation that changes no state             nobody: each game's own, never compared (piece 10)
```

## Decisions (the user, 2026-11-18)

1. **Pairs:** one claim rule for every pair; the owner decides. A player as victim cannot change owner: its game
   plays its half, follows the claim and decides nothing (the stated exception).
2. **Passengers:** seated without the paired get-on, if that does not bring back the jump and T-pose fixed on
   2026-11-18 (`party-shares-a-horse`, `riders_smooth`). If it cannot be made clean, today's rules stay, with a note.
3. **What a guest creates** (a script's `PlaceAtMe`, a summon): the host creates it. A summon gets a test.
4. **Scenery and critters:** not synced. They change no game state; the world check stops comparing them.

## What was found (the audit of 2026-11-18, checked in the code)

- Every action hand rule patches one fact, read in IDA (39004, `coop_ApplyActionGraphEffects`): beside its graph
  an action writes attack state (flags1 bits 28-31), sneak, sit state by event (2, 3, 4, 6, 8), sprint, flags2
  0x100/0x400/0x800, one graph int for default actions 97-105, and the `pa_Kill` pair with its killer. A copy on its
  stream plays no action, so it gets none of it.
- Every pair rule (`ReplayState.h`: `SettlePairedReplay`, `ResendPairStop`, `CancelledBlow`, `PairMarkClear`,
  `ContactKills`, `PoseHeld`, `HeldByPair`) exists because two games run one pair (kill-moves.md, "What broke kill
  moves": "four faults, all from each game running its half").
- The wait-for-the-copy caches are of three kinds: the actor is not here yet (actor values, effect visuals, AI
  state, the spawn's inventory waiting for its 3D); a named actor is not here yet (look targets, rides, boards,
  confiscations, alias fills); this game's own actor has no session id yet (the magic queue).
- Inventory has a snapshot at spawn (`ReadLiveState`) and equipment one after each change
  (`RunEquipmentSnapshots`); what is missing is order (an older spawn over a newer equip) and a resync.

## Piece 1: the copy record

One store on every game, keyed by session id, holding the last state its owner declared, in parts. It replaces
every cache, and makes "the copy is not here yet" a non-case: the record waits, the copy takes it when it appears.

```text
owner's game                      host                         another game
a part changes
  RecordRequest{id, part,   ->  owner check (OwnerOf)
    version, bytes}             keeps it (HostRecords)   ->  NotifyRecord -> CopyRecords[id][part]
                                                               version older than held: dropped
                                                               copy here: part's Apply(copy)
                                                               copy not here: kept
spawn of id                 <-  every part it holds      ->  NotifyCharacterSpawn carries the record
                                                               copy made, 3D ready: every part's Apply
a part names actor B (look target, mount, killer)
                                                             B not here: Apply leaves the field, marks
                                                               "waits for B"
                                                             B gets its session id here: Apply again
a copy that looks wrong (a part's own check)
                                 <- RecordAskRequest{id, part}
owner's game sends that part again (new version)
a player joins                   every record, every held-now state (piece 9), from one list of stores
```

```cpp
struct RecordPart               // one per kind: registered once, by its service
{
    uint8_t Kind;                                         // ActorState, Items, Values, Effects, AiState, Ride...
    std::function<bool(uint32_t aFormId, BytesView)> Apply;   // false: a named actor is not here yet
};
void CopyRecords::Take(uint32_t aId, uint8_t aKind, uint32_t aVersion, Bytes aBytes)
{
    Part& part = m_records[aId][aKind];
    if (aVersion <= part.Version) return;                 // R2: last state wins, order free
    part = {aVersion, std::move(aBytes)};
    TryApply(aId, aKind);
}
void CopyRecords::OnSessionId(uint32_t aId)               // a copy made, or a named actor arriving (R5)
{
    for (auto kind : m_records[aId]) TryApply(aId, kind);
    for (auto [id, kind] : m_waitsFor[aId]) TryApply(id, kind);
}
```

- A version is the owner's own counter per actor and part. A spawn's parts carry theirs, so a spawn read before a
  change can never overwrite it.
- Events about an actor that is not here are **dropped**: its record holds their result. Nothing is queued.
- Parts move in one at a time; each service keeps its message until its part exists, then its message goes.

Files: `Services/CopyRecords.{h,cpp}` (every game), `Services/Host/HostRecords` (it exists: the per-character
record grows the parts), `Messages/Payloads.h` (`Record`), `CharacterService` (spawn, `OnSessionId`).
Tests: ClientTests `[records]` (versions, waits-for), HostAuthorityTests (owner check, a joiner gets every part),
GameTests "A part sent before its copy exists is on the copy when it spawns", "A part naming an actor that
spawns later".

Deletes, as each part moves: `ActorValueService::m_copyValues`, `EffectVisualsService::m_shown`'s spawn path,
`AiStateService`'s per-session map, `CharacterService::m_pendingRides`/`m_pendingBoards`/`m_pendingLooks`,
`CrimeService::m_confiscations`' retry, `MagicService::m_queuedEffects`' wait (its 4 s expiry and head-of-line
block), `CharacterService`'s `WaitingFor3D` inventory. Replaces PR #73's `AiStateService::ResolveNewSessions`.
Stays: `GuestEngine::m_pendingAliases` (quests are on the model already; moved only if it falls out).

**As built (2026-11-18), with one part, AI state:**

```text
CopyRecords (Services/CopyRecords, no engine: ClientTests [copy-records])
  Register(kind, Apply, Read)   by the part's service, once
  Declare(id, kind, bytes)      an owner's change: the next version, sent (RecordRequest)
  ReadAll(id, form)             the announcement's parts (AnnounceCharacterRequest::Record)
  Take(id, part)                another game's: NotifyRecord, or each part of NotifyCharacterSpawn::Record
  OnSessionId(id)               from CharacterService: a copy made, its 3D ready, an actor announced
  DeclareAgain(id, kind)        NotifyRecordAsk reached its owner (RecordAskRequest, R3)
CopyRecordsService              the messages in and out; Forget on NotifyRemoveCharacter, Clear on leaving
HostRecords                     OnRecord: owner check (OwnerOf), newest version kept, NotifyRecord to the others
                                every spawn the host sends carries RecordOf(id) (HostCharacters::BuildSpawn,
                                SendHostSpawns), so a joiner gets every part; OnRecordAsk: to the owner alone
```

- `Apply` returns the session ids its bytes name that are not here; `CopyRecords` keeps them as waits-for. A copy that
  is not here returns none: `OnSessionId` of its own id applies every part it holds.
- An owner's parts are in the same store, so a game that takes an actor over (a mount) goes on from the last owner's
  version, and the last owner's late part is dropped.
- A part's bytes are its struct's own encoding (`ToPartBytes`/`FromPartBytes`, `Structs/RecordPart.h`); the host reads
  the AI state part for `HostDeath` (who fights), nothing else.
- `HostDeath` hears only the AI parts `HostRecords` keeps (`OnKept`): an older part arriving late changes nothing.
- AI state moved: `AiStateRequest`/`NotifyAiState`, the spawn's and announcement's `AiState` field and
  `AiStateService::m_owners` are gone. `GuestScene` hears `CopyAiStateEvent` (the part applied to the host's player's
  copy), so a guest that joins mid-scene plays its part too.
- `Ask` has no caller yet: no part has a check of its own that says a copy looks wrong. The path is tested (GameTests
  "A part a game asks for comes again", HostAuthorityTests "An asked part").
- Not closed: "A temporary form in a slot" (bugs.md). Its form has no session id on the **owner**, so the owner sends
  none; waits-for is the copy's side. The owner would have to declare again when the actor it names is announced.

## Piece 2: actor state is a part, not a rider on actions

```text
owner, each frame an owned actor's state differs from what it last sent:
  ActorStatePart {
    beside the graph (39004):  attack state, sneaking, sprint, flags2 0x100 / 0x400 / 0x800
    life:                      life state, in kill move + killer (38613), essential-down
    hands:                     weapon state wanted (drawn or sheathed)
    name, factions             (a script's SetDisplayName / SetName; a faction added later)
  }
copy: Apply writes exactly those fields; everything else in flags1/flags2 it leaves alone
```

One writer per bit:

```text
from its stream's own events (handlers run on the copy)   sit state (42868, 42881, 42835), knock state
                                                          (42879, 42880), weapon state 3/0 (42824, 42825)
from the record                                           the list above
from the copy's own body                                  nothing
```

```cpp
bool ApplyActorState(Actor& aCopy, const ActorStatePart& acPart)
{
    aCopy.actorState.flags1 = (aCopy.actorState.flags1 & ~kRecordBits1) | (acPart.Flags1 & kRecordBits1);
    aCopy.actorState.flags2 = (aCopy.actorState.flags2 & ~kRecordBits2) | (acPart.Flags2 & kRecordBits2);
    if (acPart.Dying && !aCopy.IsDead()) aCopy.KillBesideGraph();      // as today's dying action
    if (acPart.InKillMove != aCopy.IsInKillMove())
    {
        Actor* pKiller = acPart.Killer ? ActorOf(acPart.Killer) : nullptr;
        if (acPart.Killer && !pKiller) return false;                   // R5: applied when the killer appears
        acPart.InKillMove ? aCopy.EnterKillMove(pKiller) : aCopy.EndKillMove();
    }
    if (acPart.Drawn != aCopy.ShowsWeaponDrawn()) aCopy.ShowWeaponDrawn(acPart.Drawn);   // 42824 / 37169
    return true;
}
```

- The owner reads the fields after its own frame, so it needs no hook per action: whatever set them (an action, a
  script, the console) is caught. Sent on change only (memory: events over polling; the read is local).
- A copy off its stream (the pair exception, piece 4) still plays its owner's actions, which write these bits
  themselves through 39004; the part then agrees with them.

Files: `Games/Animation/ActorStateRules.h` (the masks, pure), `CharacterService::RunLocalUpdates` (the read),
`AnimationSystem` (the rules go). Tests: ClientTests `[actor-state]` (one writer per bit: the masks never
overlap the stream's), GameTests "A copy made after its owner sneaked stands sneaking", "...after its owner's
last action holds its state" (the travel cases), "A copy on a sync point of drawn hands shows them".

Deletes: `ReplayState::Flags1`/`Flags2` and `ActionEvent::StateAfter1/2`, `EntersKillMove`, the dying test on
`action.State1` in both replay paths, `InventoryService::RunWeaponDrawConvergence` (a 0.25 s poll) and
`RunWeaponStateUpdates`, the name's race-menu-only send.
Also here: **a copy's swing hits its owner's target.** `AiStatePart` already carries the combat target; the copy
gets it as `ExtraForcedTarget` (0x9D, read first by the victim pick 47272: read in IDA, combat-targets.md), so
the guest's game still decides whether its own player was hit (kept: the victim sees the blow land), on the
owner's choice of victim.

**As built (2026-11-18)**, after the RE (`docs/re/systems/actor-state.md`), with three changes from the above:

```text
ActorStateService (every game)
  owner, each frame, each LocalComponent actor:
    kActorState  flags1 & (sprinting | sneaking | life), flags2 & wantBlocking,
                 in kill move + killer's session id, hands (settled: drawn / sheathed), tick
    kName        its display name                      declared when it differs from the record's
  FactionsChangeEvent -> kFactions
  copy: kActorState applied when its stream's playback reaches the part's tick (off its stream: at once)
        kFactions once its 3D is in; kName if it reads differently
```

- **Attack state, sit state and recoil stay on the stream.** 39004 writes only their start; the copy's own graph
  handlers carry them on (attack 42810-42814 and 42828, sit 42868/42881/42835, recoil 42815). A part arriving out of
  order with those handlers would rewind a swing. An action on a copy's stream gives its owner's flags but the part's
  bits (`ActorStateRules::FromSnapshot1/2`), so every bit not read yet travels as before; `StateAfter1/2` stay. Death
  stays there too (with its clip). Sprinting (bit 8) is not in the part: its writer on a copy is not read.
- **Kill moves:** two copies of one pair still enter at the action on the stream, in one frame (from their two parts
  they were two frames apart, `host-kill-move`). The part enters one (38613, its killer once here:
  `CopyRecords::WaitFor`) for a copy that missed the action, and takes a copy out of one its owner left when no pair
  plays, its killer cleared first (38614 kills only with a killer).
- **No `ExtraForcedTarget`.** 38608 takes it only while `IsInCombat` is false and skips cone and reach, so a copy
  would hit at any distance. The copy's `currentCombatTarget` (`+0x104`) is already its owner's target, and 47272 then
  hits only that actor within reach: the owner's choice, judged where the blow lands.
- Names: the party's lists follow the name part of each player's character (the host sends `NotifyPlayerName` from
  it; `PlayerNameRequest` is gone). A host's quest renaming an actor another game owns goes to that owner
  (`NotifyActorName`), whose part carries it.
- Gone: `ReplayState::Flags1/Flags2`, `DrawWeaponRequest`/
  `NotifyDrawWeapon`, `RunWeaponStateUpdates`, `RunWeaponDrawConvergence`, `Request/NotifyFactionsChanges`, the spawn's
  `DisplayName`, `FactionsContent` and `IsWeaponDrawn`, `CacheComponent`, `HostCharacters::SetPlayerName`.

## Piece 3: items are their owner's, as counts

```text
any game's player changes an actor's items
  its own character      its engine does it (it is the owner)     -> ItemsPart{version, item: count...}
  another game's actor   ItemTransferRequest{from, to, item, n}  -> the host runs it on its engine
    (an NPC, a corpse)   (today's ContainerTransferRequest, grown to give as well as take)
                         the host's own change of its NPC        -> ItemsPart of that NPC
  a guest's engine changed a copy by itself (a script, a perk)   -> not sent; the next ItemsPart puts it back
copy: Apply sets each item to its owner's count (add or remove the difference), then worn items
  with no 3D yet: kept in the record, applied once at 3D (one path, the newest version)
```

```cpp
void HostAuthority::OnItems(const RecordRequest& acPart, ConnectionId_t aSender)
{
    if (m_characters.OwnerOf(acPart.Id) != aSender) return;          // was: relayed from anyone
    ...
}
bool ApplyItems(uint32_t aFormId, const ItemsPart& acPart)
{
    for (const auto& [item, count] : Difference(m_formBridge.GetActorInventory(aFormId), acPart.Items))
        m_formBridge.ObjectAddOrRemoveItem(aFormId, {item, count});   // under ScopedInventoryOverride
    m_formBridge.SetWorn(aFormId, acPart.Worn);
    return true;
}
```

- A change sends the changed entries with the new version; a spawn and an ask send all. A copy that finds its
  version skipped asks (R3).
- Party items and the purse stay as they are: the host owns them and says each player's count.

Files: `InventoryService` (owner-only sends, `ApplyItems`), `HostItems` (transfers both ways), `HostAuthority`.
Tests: HostAuthorityTests "an item change from a game that does not own the actor is dropped", GameTests "A
looted corpse's copy is as empty as the corpse" (either looter), "An equip sent after the spawn was read
survives it", "A copy spawned again wears what its owner wears".

Deletes: `HostAuthority::OnInventoryChanges`'s relay from anyone, `RequestEquipmentChanges`/`Snapshot` and
`EquipmentSnapshots` (one part instead), `CharacterService`'s `WaitingFor3D` inventory.
To check when building: whether `RunNakedNPCBugChecks` (a 1 s poll) still has anything to catch.

**As built (2026-11-19)**, after the RE (`docs/re/systems/inventory-changes.md`, "1.7.104"):

```text
InventoryService (every game), RecordKind::kItems = ItemsPart {Full, Inventory: stacks by count, worn flags, hand spells}
  owner: an item or equip hook, a 3D load, 418622's reset, a host grant  -> the actor touched
         once a frame, each touched actor it owns: ItemsPart::Change(held, now)  -> Declare (only the stacks that changed)
  copy:  the part applied once its 3D is in (waits for it), and again on each 3D load (ReferenceLoadedEvent), 418622
         (WornResetEvent) and CharacterService's move to its owner's cell
         each item: SetCopyItemCount (count, stacks with extra data; worn and charge aside), then ApplyWorn:
         only the pieces that differ off and on, each hand holding its worn weapon or torch and its spell; the full
         reset (EquipmentRepair) when one is refused; not settled: tried again every 0.25 s for 5 s
CopyRecords / HostRecords: MergedPart  a change merges onto the version just before; else the owner is asked (R3)
HostAuthority::OnInventoryChanges      the party's gold and items; a host script's change of another game's actor to its
                                       owner alone (a quest taking a guest's claw), whose part follows
HostItems::OnContainerTransfer         the loot to the looter alone; the NPC's change goes out as its part (ItemsChangedEvent)
```

- Read: leveled lists stay out of the part (a base container's, listed by `GetInventory` as they are); for a player's
  character, gold and party stacks too (the host's).
- A change carries absolute counts per stack (0: gone), merged only onto the version before it; a spawn, an
  announcement and an ask carry the whole part. A copy missing a version asks (`CopyRecords::AskWith`), as does the host.
- Stacks are matched ignoring charge (`ItemsPart::SameStack`): an enchanted weapon's falling charge sends nothing and
  never makes a copy take its sword off and on.
- A guest's take no longer passes through the host's copy of the looter: `HostEngine::TransferContainer` changes only
  the container, and the looter's game changes its player. A put still moves from the host's copy of the player: its
  count is spent at once, so a second put before the player's next part finds nothing, and the copy's real list (worn,
  a roll's) goes to the container.
- A player's part carries its party stacks too (their worn state); a copy applies only the private counts and keeps
  the party stacks the party's store gave it. 16059's target only moved the item and marked a theft
  (inventory-changes.md, "What 16059's target does"); a stack now carries its owner (`Inventory::Entry::ExtraOwner`),
  so the grant marks a theft on the looter's own game. The vampire lord's model fix still lands after
  the copy's items at its 3D (`CopyRecords::ApplyAgain`).
- Only a player character's item changes still go to the host as events (the party's stores); an NPC's go only as
  its part.
- `RunNakedNPCBugChecks` stays, for this game's own actors only (a copy's reset did nothing already); it logs when it
  resets one, to learn whether it still catches anything.
- The harness's drift for a copy is now the engine's own (`reset_worn`, 418622) instead of `strip`, which took a copy's
  armor off past every hook, a drift no engine path makes on a copy (its unequip hook refuses the engine's).
- Gone: `RequestEquipmentChanges`/`NotifyEquipmentChanges`, `Request/NotifyEquipmentSnapshot`, `RunEquipmentSnapshots`
  (the 3 s heartbeat) and `EquipmentRepair::Snapshots`, the relay of item changes from anyone, `WaitingFor3D`'s
  `SetActorInventory`, the spawn's `InventoryContent` and `ActorData::InitialInventory` (the record's part instead).

## Piece 4: pairs are claimed

```text
a game's own actor starts a pair with an actor it does not own (32803, at once: no waiting)
  -> PairClaimRequest{actor, partner, tick}
host: first claim naming either actor wins (HostPairs, as HostHands::m_holds)
  partner is a host NPC, free    its owner becomes the claimer for the pair (TakeOwnership, as a mount)
                                 NotifyPairClaim to all: the host's NPC is now a copy on the host
  partner is a player            NotifyPairClaim: its game plays its half and decides nothing
                                 (no mark sweep, no contact kill; today's replay)
  either actor already claimed, dead, or its graph not running (kProcessMe clear)
                                 NotifyPairRefused -> the claimer ends its pair: EndPairsOf (32809),
                                 pairedStop until its clip leaves, its blow dropped, its kill cam ended
every other game: both actors are copies: each replays its stream (PairOnStream, as today)
the pair ends (PairEnd, KillActor, or the claimer silent 2 s) -> PairDoneRequest
  host: the NPC is the host's again, from the claimer's sync point
  a kill: the claimer sends the blow as a hit (today's forwarded hit); the host's KillImpl kills it
          (death items, credit and the death stay the host's)
```

```cpp
void HostPairs::OnClaim(const PairClaimRequest& acClaim, ConnectionId_t aSender)
{
    if (m_characters.OwnerOf(acClaim.Actor) != aSender) return;
    if (Claimed(acClaim.Actor) || Claimed(acClaim.Partner) || !m_game.CanPair(acClaim.Partner))
        return m_network.Send(aSender, NotifyPairRefused{acClaim.Actor, acClaim.Partner});
    m_claims.push_back({acClaim.Actor, acClaim.Partner, aSender});
    if (!m_characters.IsPlayerCharacter(acClaim.Partner))
        m_characters.Lend(acClaim.Partner, aSender);                  // HostCharacters: Owner, NotifyOwnership
    m_players.SendToAll(NotifyPairClaim{acClaim.Actor, acClaim.Partner});
}
```

Build in two steps:

1. **Mounts.** They ask the host already (`MountRequest`). The claim is made at the get-on's start and the
   horse goes back only at `Detach` (41539) **after** its exit pair's `PairEnd`: never mid-pair. A handover to a
   passenger waits for that too.
2. **Kill moves.** Host NPC victims first (the common case), then the claim as the arbiter for player victims.

- Crossed kill moves: the host's first claim decides, for any number of players. The loser gets
  `NotifyPairRefused` and yields as today; both games no longer have to compute the same answer from ticks.
- A copy in a claimed pair on a third game needs nothing new.

Files: `Services/Host/HostPairs.{h,cpp}`, `HostCharacters` (`Lend`, `GiveBack`), `CharacterService`
(`OnNotifyPairClaim`, `OnNotifyPairRefused`), `AnimationSystem` (the local-partner path shrinks to the player
victim), `Games/Animation/ReplayState.h`.
Tests: HostAuthorityTests "the first claim on an actor wins", "a claim on a frozen NPC is refused", "a claimer
gone gives the NPC back"; GameTests "A guest's kill move on a host NPC runs on the guest's game alone", "A
refused pair ends on the claimer with no blow", "The horse changes owner only between pairs" (the fake's pair
instance learns `PairEnd` from its instance alone); scenarios `client-kill-move`, `kill-moves-cross`,
`frozen-wolf-kill-move` (PR #73's), `horse-passes-between-riders`, `party-shares-a-horse`.

Deletes, for an NPC partner: `PairMarkClear` and `HookTryMoveTo`'s pass, `ContactKills`, `PoseHeld` and
`HoldCopyRagdollStill`, `HeldByPair`, the manual `PairEnd` (`Actor::EndMountPair`). For every pair:
`SettlePairedReplay`'s tick comparison and `ActorExtension::LatestPairAction` (the host's answer instead).
Replaces PR #73's `PartnerAnimates` (the host refuses the claim) and, for NPC victims, PR #75's
`TakesOwnerBodies` (the victim is local where the pair runs).
Stays, for a player victim only: the yield's four steps (`ResendPairStop`, `CancelledBlow`, the kill cam's end)
and the victim's half replayed on its own game.

## Piece 5: passengers sit at once

```text
our player activates a horse a party player drives (HookActivateMount -> BoardEvent)
  -> BoardRequest
host: seats free -> NotifyBoard to all, the asker included
every game, the passenger's own too: PutOnMount + HorseEnterInstant (39004: sit state 3 at once)
  no paired get-on clip anywhere, so nothing to end it and nothing for a copy to replay up there
passenger gets off: its own Detach -> UnboardRequest -> NotifyUnboard (as today)
```

The bug not to bring back (player-mount.md, 2026-11-18): a copy seated at once jumped 174 units to the saddle
and then played its owner's whole get-on up there. That came from the copy sitting at once while its **owner**
still climbed. With the owner sitting at once too, no climb is in its stream. What remains is the owner's own
pop onto the saddle.

Gate: `party-shares-a-horse` with `riders_smooth` on both games, plus a new check that no rider's graph plays a
get-on clip while seated. If the pop cannot be made clean (a fade or blend), **today's rules stay** and this
section says so; the claim rule of piece 4 still removes the handover's `PairEnd` send.

Deletes if it holds: `SeatsAtHoldEnd`, `SkipsMountEntry`, `GettingOnMount`, `KeepDriverOn`'s re-seat,
`m_pendingBoards` (the Ride part of the record).

## Piece 6: the host runs an activation's world part

```text
our player activates a reference (19796)
  its own screen's part, here:   a container's menu, a book, a sign's text, dialogue, a ride (pieces 4, 5)
  the world's part:              ActivateRequest -> the host runs Activate with that player's copy
                                 (guest scripts: the player in its scripts is the activator)
                                 -> what changed goes out as rows (HostWorldState), to every game
other guests: nothing replayed (NotifyActivate goes only to the host's game)
a row for a reference with no 3D: kept, applied when its 3D loads (PR #77's lid rule, for every row)
```

Files: `ObjectService`, `HostAuthority::OnActivate`, `TESObjectREFR.cpp` (`HookActivate`), `ReferenceStates`.
Tests: GameTests "A lever a guest pulls moves once, on the host, and every game shows it", "A guest's chest
lid follows the host's row"; scenarios `door-opens-for-both`, `relief-takes-client-relics`.
Deletes: `Delivery::FromAnyone` for activations, `ObjectService::OnActivateNotify` on guests, the door's
pre-activation open-state compare. Also here: **the host's NPCs' lock changes go out as rows** (`HookLockChange`
19512 raises every change; only a guest's own player's is sent today).

## Piece 7: the host creates

```text
a guest's script places a reference (PlaceAtMe), or its player's spell summons an actor
  -> CreateRequest{base, place, count | spell, caster}
host: creates it (its engine) -> a create row, or an actor's spawn with its commander
guest: its script's call returns the host's reference once the row is here
       its own engine's summon is suppressed (the copy of the host's is the familiar)
```

Tests: GameTests "A guest's familiar is one actor on every game" and a scenario (a guest casts Conjure
Familiar: both games show one, it fights for the guest); `vampirism_flows.cpp` "VampireChange's marker is the
host's".
Deletes: the guest-only reference the world check trips on; `RunActorDiscovery`'s silent delete of a guest's
temporary actor becomes a refusal that logs.

## Piece 8: the eye is the host's

```text
host, each guest's copy: highest detection level of it among its NPCs (41408's walk, for that copy)
  changed (hidden / seen, and the level in steps) -> NotifyEye{player, level, seen}
guest: 40593's read of 41408 answers that level; every NPC copy is an unseen witness
  (DetectionRules::Installed: today's rule for players' copies, for every copy)
```

Blocked on a read: host NPCs get no line of sight to a still guest's copy 74 units away (bugs.md, "A still
guest unseen up close"). Until that is understood the host's level would be wrong, so that entry comes first.

## Piece 9: held-now state for a joiner

```text
cutscene controls and fades held    HostPlayerNatives keeps the calls not yet ended -> sent on PlayerEntered
map waypoints                       the host keeps the last per player            -> sent on PlayerEntered
an open conversation                HostDialogue's current one                    -> sent on PlayerEntered
a channelled spell                  CastPart{spell, source, target} in the caster's record (piece 1):
                                    a copy made mid-cast starts it; a stop is the part cleared
```

`HostAuthority::PlayerEntered` sends from one list of stores; a store not on the list fails a HostAuthorityTests
case ("every store answers a joiner").

## Piece 10: ragdolls follow the pile rule; scenery is nobody's

- **A copy's ragdoll** keeps its own Havok bodies, steered to its owner's frames (as now), and joins the
  "played" collision group of `Physics/BodyGroups.h`: it meets the world and characters, not other played bodies
  or loose objects nobody moves. Its rest follows the pile's sleep rule (`RestSlept`), which is what the two
  open rest bugs miss (woken by the steering after its first sleep). Its own section in physics-sync.md when built.
- **Scenery, critters, ambient animation:** each game's own. The world check (`world_state`, the end
  comparison) leaves out: references of a critter spawner, FishController activators, FLOR and ACTI node poses
  with no open state, and created references whose base is a critter. A critter a player catches is that
  player's loot, as any pickup.

## Build order

```text
1  copy record (store, versions, spawn, ask, waits-for)          everything below uses it
2  actor state part; a copy's swing target                       deletes the action riders
3  items part, owner-only                                         the biggest bug group
4a pair claims: mounts          4b pair claims: kill moves
5  passengers seated at once (or kept, noted)
6  activation's world part on the host; lock rows; rows at 3D load
7  the host creates; a summon's test
9  held-now state for a joiner; the cast part
10 world check leaves scenery out (a harness change: can go first, it unblocks end comparisons)
8  the eye (after "A still guest unseen up close" is read)
10 copy ragdolls in the played group
```

Each step: its GameTests flow first, failing, then the change, then the deletes in the same PR, `scripts/check.sh`,
and only the scenarios it names on the duo.

## What stays an exception

- **A player as a pair's victim** (decision 1): its character cannot change owner. Its game follows the claim.
- **A hit on a player is judged on that player's game** (`HitRules`): the victim sees the blow that lands. It
  is one decider; only the swing's target becomes the owner's.
- **Arrows and one-shot effects as events,** cutscene calls relayed to every game: left as they are.
- **Passengers,** only if piece 5's gate fails.
- **Scenery:** not synced at all (decision 4).

## Open questions (engine facts nobody has read)

1. Can an NPC mid-swing on the host be made a copy cleanly when a claim arrives half a round trip late
   (`TakeOwnership` during an attack; its combat controller)?
2. What a claimer's player sees when `EndPairsOf` (32809) comes 100-400 ms into its clip (measured only for the
   clash yield, at once).
3. Does `HorseEnterInstant` take on a player's graph beside a ridden horse? The 2026-11-17 probe's `sae` never
   reached the graph, so it proved nothing (player-mount.md).
4. Per activator type (door, container, activator, furniture, flora, lever scripts): which part of `Activate`
   (19796 and each form's vfunc) changes the world and which only this game's screen.
5. How a guest's script call can wait for the host's created reference (the VM's latent calls), and what a
   summon's own engine path creates before it can be suppressed.
6. Who applies a host NPC's weapon enchantment to a guest's player (the audit's "inferred"; not read).
7. Why the host's detection gets no line of sight to a guest's copy (piece 8's blocker).
8. Whether a copy's ragdoll in the played group still lies right on stairs and slopes (its bodies meet the world).

## Acceptance: the open bugs each piece should close (docs/bugs.md, 2026-11-18)

81 entries: 40 the model's, 17 engine quirks, 13 test tools, 9 outside it or unread, 2 inside physics and the
stream themselves.

- **Piece 1 (2):** "A copy that looks at nobody" (closed 2026-11-18: GameTests and the duo scenario `guest-rejoins-while-joined`), "A temporary form in a slot" (open: above).
- **Piece 2 (7):** "An action sent before its copy exists", "A seated owner's copy keeps an old action", "The
  attacker's copy not in its kill move", "A mod's rename at run time" (both), "A character's base bytes after
  joining", "An NPC copy's swing picks the nearest".
- **Piece 3 (6):** "A looted corpse keeps items on the guest", "A spawn's inventory over a newer equip", "The
  guest player's copy wears nothing", "A guest joining again does not wear what it wore", "A copy without its
  weapon", "References differ" (the leveled containers).
- **Piece 4 (5):** "Crossed kill moves: a short winner's blow lands first", "A kill move that never pairs", "A
  frozen NPC a guest's kill move missed", `host-kill-move`'s missing `KillActor`, "A passenger jumps 57 units as
  it takes the reins". To watch: "After a kill move the guest's own body floats" (unread).
- **Piece 6 (4):** "A chest lid's pose", "The host shows a guest's relief box", "Doors the host's NPCs unlock",
  "The host's roll for a guest's loot".
- **Piece 7 (2):** "A guest's summon", "References a guest's script creates".
- **Piece 8 (2):** "The guest's eye is its own estimate", "The sneak eye with the other player looking".
- **Piece 10 (10):** ragdolls: "Copy ragdolls at rest", "A dead player's copy a little off its owner's body", "A
  knocked-down copy placed 62 units off", "A copy that stays off its owner"; scenery (closed by not comparing):
  "Riverwood's scenery drifts apart", "Whiterun plains' critters differ", "Cover plates after the host leaves",
  the fish and plants of "End comparisons in towns" and "After a guest rejoins", "Clutter flags after a vampire
  rejoin" (to check: loose clutter is physics, not scenery).
- **Not this design's:** "Headtracking on copies" (a copy's own ambient pick, 38012), "Animation mods' random
  picks", "Sync points asked for over and over", "Physics against vanilla", and the engine quirks, test tools
  and unread entries.

## The open PRs

- **#73:** `ResolveNewSessions` is replaced by piece 1's waits-for; `PartnerAnimates` by piece 4's refusal. Merge
  it now for the fix; its tests and `frozen-wolf-kill-move` become those pieces' acceptance.
- **#75:** `TakesOwnerBodies` is replaced for NPC victims by piece 4 and stays for a player victim. Merge it.
- **#77:** not replaced: a row applied when the 3D loads is piece 6's rule for every row. Merge it.
- **#74:** docs and a scenario. Merge it.
