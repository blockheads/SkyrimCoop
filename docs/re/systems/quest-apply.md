# Applying a quest's state: a stage without its fragment, alias fills, objectives (1.6.1170)

How a guest takes the host's quest (world-state sync step 4, `docs/design/world-state-sync.md`) with the
engine's own calls, and how the engine itself restores a quest on a save load. Read in IDA 2026-10-20;
names in `docs/re/ida_notes.tsv`. No SKSE plugin in `reference/plugins` does this: PapyrusExtender only reads
`executedStages` and objectives.

## Setting a stage

```
TESQuest::SetStage 25004 (quest, stage)            false unless running (flags & 1) and the stage exists
  currentStage = max(currentStage, stage)
  TESQuestStage::Execute 25252                     the stage's done bit (flags |= 1)
    per log entry: TESQuestStageItem::Execute 25260
      conditions (against the player; failed: only the done event below)
      item flag 1: SetCompleted 24991              Completed, QuestStatus event, the HUD's message
      item flag 2: SetFailed 24988                 Completed|Failed, the HUD's message
      journal entry: PlayerCharacter 40509, TESQuest 25076
      TESQuestStageEvent {callback, quest, stage, item}, sent on this thread
        ├─ SkyrimVM::ProcessQuestStageEvent 53987  FindQuestFragment 53429; a fragment: run it, then the callback;
        │                                          none: the callback at once
        └─ other sinks (SKSE plugins, mods)
      callback (StageItemFinishedCallback, 25234) → TESQuestStageItemDoneEvent → BGSStoryTeller (start-ups wait on it)
  AddChange(Stages)
```

**A stage without its fragment** (`TESQuest::SetStageWithoutFragments`): `SetStage` with 53987 hooked to take
its own no-fragment branch, the item's callback at once, while the flag is set on this thread. Everything
else of the stage happens as on the host: journal, completion, the events mods listen to. Papyrus' `SetStage`
(`SetCurrentStageID` 56684) starts a stopped quest first (`EnsureQuestStarted` 25003); a guest never does:
it takes the host's running flag instead (below).

## What a save load does

`TESQuest::LoadGame` (25030, `quest-change-form.md` for the layout) only writes fields: each stage's done bit,
`currentStage` = the highest done stage, each objective's state byte (+0x1F, found by index +0x1C), the quest
flags, the reference alias map (`refAliasMap` 0x70, through the hash map's insert) and location aliases. No
event, journal entry or message. A reference's side of an alias (its `ExtraAliasInstanceArray`, factions,
packages) comes back with the reference's own change form. A guest joining takes the host's quests this way
(`NotifyQuests::Join`), except fills, which need the reference's side too.

## Alias fills

- `ForceRefIntoAlias` (25052, CommonLibSSE-NG; SKSE `ForceRefTo`; Papyrus `ReferenceAlias.ForceRefTo`):
  `GetRefAlias` 25042, clears the old fill (`ClearRefAlias` 25051), promotes the reference
  (`BGSDynamicPersistenceManager::PromoteReference`), fills the map, `BGSRefAlias::InitializeRefAlias` 24011
  (the alias's data onto the reference). A guest uses it for the host's fills.
- **Neither 25052 nor 25051 records a change.** Only a start's fill (25048) and 25049 record `RuntimeData`
  (`0x10000000`), so a script's `ForceRefTo` never reaches the change funnel (14607). Quest sync compares
  snapshots instead of watching the funnel. Side effect: a guest's fills are not in its own save.
- The reference has to be in the guest's memory. One the host promoted when it filled the alias (an NPC in
  an unloaded cell: Gadnor, a bounty target) may not exist on the guest: that fill cannot be made until it does.
- A stopped quest's aliases are cleared (25049): the host sends none, the guest clears its own.

## Objectives and flags

- `BGSQuestObjective::SetState` 23933 (objective, state): behind `SetObjectiveDisplayed/Completed/Failed`
  (56681-56683). Needs the quest running or starting; records `Objectives`; state 1 shows the HUD's
  quest-started and objective messages.
- Running (0x1) and starting (`StageWait` 0x80) are written directly, as `TESQuest::SetEnabled` in
  CommonLibSSE-NG does: no start machinery, so the guest rolls nothing. Completed and Failed go through 24991 /
  24988 for their messages. The rest of the flags (the journal's tracked quest 0x800, shown in HUD 0x20) are
  each player's own.

Measured again for quest rewards (2026-10-27): real host and guest turn-ins of Finn's Lute, The Book of Love and
The Golden Claw produce the guest's native stage, stage-item-done and QuestStatus completion events, without a
reward fragment (`TestHarness/QuestFragments.cpp` counters, [quest-rewards.md](../../design/quest-rewards.md)).
TESQuestStartStopEvent is a separate signal, not a completion event; stop counts can differ on the guest's flag
application path. No installed Experience DLL result is claimed by these event measurements.
