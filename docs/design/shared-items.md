# What the party shares

Plan, 2026-11-23, agreed with the user ("what we need is a 'party inventory' and then all these simply
become operations on the party inventory"). Built: steps 1 to 3 (below). It replaces the "party's pack" plan (hear, count and take across
personal pockets) and takes in `party-items.md` and `party-purse.md`, whose rules for players stay.

**The host's player holds the party's real items. Every other player and copy shows a mirror.** Gold and party items
work this way already, through two copies of the same code (`HostPurse`, `HostPartyItems`, until step 1); this makes it one, and the
only way.

```text
                 HostPartyItems (host)
                 the real stacks, on 0x14, aliases and all
                /            |              \
     guest A's player   guest A's copy   guest B's ...     mirrors: "[P] <name>", set to the count

any player's action
  -> what does the party own?   Owns(item)         one rule
  -> the host does it to the REAL stack            Add / Remove / Change
     the engine raises its own events on 0x14:     OnItemAdded, filters, aliases, stages, OnContainerChanged
  -> every mirror set to the count                 NotifyPartyItem, as now
```

So the story needs nothing special for what the party owns: it hears 0x14 get it, counts 0x14's (each mirror equal),
and takes from 0x14.

## What the party owns

```text
Owns(item, how it came):                            decided on the host, at each arrival
  gold
  a key, or a story gift (a quest, alias, dialogue line or scene script's AddItem)        party-items.md
  a stack any quest alias holds (ExtraAliasInstanceArray), a quest object or not          new
  an item a player alias's inventory filter names (SkyrimVM+0x8950, by the alias's handle) new
otherwise: the finder's own
```

Decided (the user, 2026-11-23): quest-held gear is shared as story rewards are (Skyrim.esm: 8 armor, 3 weapons without
the quest-object flag: the Forelhost mask, the Staff of Magnus, Hircine's ring...). Counted hand-ins no script watches
stay personal: Dwarven Cogs (`MGRArniel01`), soul gems (`MGR11`), practice arrows (`dunArcherQST`), the three of ten
Skyrim.esm quests whose dialogue asks for more than one of something.

No list goes to the guests: each player's change reaches the host already (`RequestInventoryChanges`).

## The operations

```text
struct HostPartyItems (host; step 1 took in HostPurse + HostItems' party branches)
  Add(stack, source, by):
    ScopedCause(by's copy)                     # events run for that guest: read before their arguments
    source a container, body or loose reference -> the engine moves the real list to 0x14 (alias kept)
    source only the guest's game (craft, buy, harvest) -> 0x14 gets a new stack
    tag it in place ("[P] "), mirrors, chat

  Remove(stack, n, to, by):
    ScopedCause(by's copy)
    to a container or NPC  -> RemoveItem(0x14's list, target)
    to the ground          -> the host's player drops it at by's place     (same interior or worldspace)
                              else through by's copy: 0x14 -> copy -> drop; its cell not loaded -> waits
    used up, taken by a script -> RemoveItem(0x14's list)
    mirrors

  Change(stack, to):  temper, enchant             -> 0x14's list, mirrors (as now)
  Use(item, by):      read, equip                 -> the real item's event, for by            (RE first)
```

A guest's arrival, so nothing blinks:

```text
guest's player gets an item (its own game's, shown at once)
  -> RequestInventoryChanges -> host: Owns? no -> its own, done
                                      yes -> Add -> NotifyPartyItem
  guest: its stack is renamed in place (worn and favorite kept), then the count set
today: it takes the item back at once and gets the mirror a round trip later
```

## What goes

```text
MoveAliasStack's copy branch, DropAliasItem, TakeAliasItem, CopyGrantedEvent + InventoryService::m_granted,
RequestInventoryChanges::Held / NewPartyItem / StoryGiven's deciding on the guest (StoryGiven stays as a fact),
HostPurse beside HostPartyItems, HostItems::TakeQuestObject's own path, TagAliasStack's alias-only rule (any stack)
stays: the move to 0x14, the chat's finder, the flows and scenarios (the journal's now expect it shared)
```

## Read, and still to learn

| Question | Answer |
|---|---|
| Can 0x14 drop it at a guest's place? | Read: `InventoryChanges__PlaceDropped` `0x140230E10` takes the position given, always the dropper's own cell and worldspace. Measured on a copy: a dropped alias item is its own reference again |
| Is the guest credited? | Read: `CauseOfCall` takes `ScopedCause`'s player before any argument; 16149 and its VM sink 53966 run inside the add. To measure with one scenario |
| Does the arrival blink? | Read in our code: the take-back does, for a round trip. Renaming in place: to see in Skyrim |
| The filters' map: removal, a stopped quest, a load | RE first (53950 read: add only) |
| A *placed* quest item a guest picks up | Not measured: how the host's reference learns of it |
| Reading or equipping a mirror | RE first: which event the real item needs |
| Unfiltered `OnItemAdded` scripts (about 12: `TGRNTPlayer`, `MS12bPlayerScript`...) | Hear 0x14 only. Maybe by the forms their scripts name; not looked at |

## Cost, said plainly

- A watched item is everyone's while its quest runs (Nightshade in `FreeformRiften04`); leftovers stay `[P]`. What
  players already carried when the watch starts is merged into the count: to decide when built.
- The host's player must be loaded where a chest or body is for a real move; the tether makes the rest rare.
- A guest's mirror has no alias: anything only the real stack can do needs an operation.

## Build order, each with its tests first

```text
1  done: one store, HostPartyItems (HostPurse is gone), no behavior change               HostAuthorityTests as they are
2  done: a guest sends a plain arrival with what it saw (StoryGiven, AliasHeld); HostPartyItems decides by
      PartyItems::ArrivesAsParty and tells the finder to rename its own (NotifyPartyItem::Converted); the host's own
      arrivals are renamed in place (InventoryService::RenameAsParty, TagStackInPlace: an alias's or a worn or
      favorite stack where it is, never a named one). Nothing is taken back. A container grant asks no rename
      (the item is not in the finder's pack yet). The host installs the decision (SetArrivalDecider)
      GameTests: a guest's key is never out of its pack
3  done: alias items all to 0x14 (MoveAliasStack, TakeQuestObject: any alias; InventoryChangeEvent::AliasHeld);
      a guest's drop drops the host's real stack where its copy stands (HostEngine::DropPartyItem, HostPartyItems'
      m_dropped, HostItems::Make adopts it); copy paths deleted (DropAliasItem, TakeAliasItem, CopyGrantedEvent)
      a guest's store of a mirror puts the host's real stack in (HostEngine::TransferContainer's put; IsQuestItem),
      a quest object's too; with no real stack left it stores nothing, never the mirror
      a drop the host's player cannot make (another interior, a quest object) is refused: the real stack kept, the
      dropper's own drop given back (HostPartyItems' m_refused, HostItems::Make); a pickpocket keeps kSteal's owner
   left: a sale, which reaches the host only as a count change (bugs.md, "The party's sword unnamed at a merchant")
      scenarios: golden-claw-guest, alias-item-guest-drops, alias-key-taken-*
4  watched items (RE the filters first)                                                NN01: a guest's root counts, hand-in
5  Use (RE first)                                                                      Arvel's journal read by the guest
6  audit: a placed quest item, MQ103's Dragonstone, a kill move on Arvel, the early quests with the guest taking
```

Steps 1 to 3 replace what this branch holds and ship together; 4 to 6 each on their own.
