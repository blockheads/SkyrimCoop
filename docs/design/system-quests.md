# System quests: one per player

Built 2026-10-28 (the user: "per-player makes the most sense"; the plan reviewed and approved the same day).

A **system quest** tracks one player's body over time, not the story: vanilla's `PlayerVampireQuest` (the hunger), a
survival mod's needs quests later. Each game runs its own instance of it, for its own player. Vampirism is its first
user ([vampirism.md](vampirism.md)).

It replaced the shared vampirism built the same day. There, a guest's disease calling the host's quest ran the
function's opening on the guest too, because routing was decided per native call, never at the call into a quest.
Here the decision is made once, where a script calls into a quest's function.

## The shape

```text
            host's game                     guest's game
story       MQ101, VC01 (Falion), DLC1VQ02  (synced copies, scripts off)
quests      run here only

system      PlayerVampireQuest              PlayerVampireQuest
quests      = the host's player:            = the guest's player:
            its hunger, stage, globals      its own hunger, globals
            (not synced)                    (not synced; kept in its record)

own quests  -                               beast form (world-state-sync.md)
```

## The rules

```text
1. learning: a call from this game's player's own script (a script on the player,
   an effect on it: OwnEvents::IsAboutPlayer) or another system quest's into a
   quest's script function (not its own quest's call, not a guest's own quest)
   makes that quest a system quest                   (PartyRules::LearnsSystemQuest)
     the game that learns it tells the others through the host
       (SystemQuestsRequest, relayed as NotifySystemQuests)
     every game's save keeps the list (the co-save's SYSQ record): a joining
       guest has it as the host's save loads

2. each game runs its own instance, for its own player
     a guest's event gate delivers its events as its own (WorldEvents)
     its natives act here, on this game's player (PartyRules: InSystemQuest);
     on a guest a world change goes to the host, but one to the quest itself or
     to its globals (SystemObject) stays here

3. a call into a system quest's function, from outside it  (RouteOfSystemCall)
     another game's relayed call                 -> here
     a player's own action (its disease, its feed)
       on that player's game                      -> here
       on the host, for a guest                   -> that guest's game, not here
     the story, or no player's action (Falion)   -> every game, each on its own player
     the story's call returning a value           -> answered by the host's instance

4. out of the world sync: its stage and flags, and the globals its scripts hold
   (the TESGlobal properties of its scripts, read when it is learned), not the
   calendar's (GameDaysPassed stays the host's clock)

5. each player's to keep: a guest's record (PlayerRecord::Extras["system-quests"],
   SystemQuestState) holds each instance's script variables, its update
   registrations (as their natives set them) and its globals' values
     put back after the join's load (JoinBridge::ApplyPlayer)
     an instance with none saved starts over (VM ResetAllBoundObjects): the
     save's is the host's player's
```

## The call hook

`BSScript::Internal::ScriptFunction::Call` (105466) is the same vtable slot (0xF) as `NativeFunctionBase::Call`
(104651). The VM's dispatcher (`sub_14144E370`) pushes the callee's frame first, then calls it:

- kCompleted takes the same return-from-frame path a native's does (`sub_104854`), so a call sent to another game
  ends here unrun.
- kInProgress leaves the stack waiting, as for a latent native, so a value-returning call waits for the guest's answer
  (`PlayerNatives::SendToGuest`, `ReturnFromLatent`).

Evidence and the VM's other slots used here: `docs/re/systems/papyrus-guest-scripts.md`, "Calls into a quest".

## Vampirism under it

```text
guest's disease (VampireDiseaseEffectScript, an effect on its player)
  -> PlayerVampireQuest.VampireChange(player)
     learned (the guest tells the host; every game learns it)
     the guest's own action: runs on the guest's game
       SetRace, AddSpell, its abilities           -> the guest's player
       PlayerIsVampire.SetValue(1)                -> the guest's own global
       PlaceAtMe(XMarker), VC01.SetStage(25)      -> the host (world)
     the host's copy of the guest respawns as a vampire (its looks changed)
     the host's player: as it was

a guest feeds on the host's NPC: the host runs the feed's fragment for it
  -> VampireFeed() goes to the guest's game: only its hunger resets

Falion's cure (VC01 stage 200, the story): VampireCure(Game.GetPlayer())
  -> every game, each on its own player: every vampire is cured
     (VampireCure changes the race only of a vampire; on the others it removes
     spells they lack and sets PlayerIsVampire to 0)

Harkon's and Serana's gifts (the story) turn the whole party, by the same rule.
```

Decided with the user (2026-10-28): the story's cure cures all vampires, not only the player who picked the line
("have the cure quest cure all vampires for now"). This also answers two vampires at once: vanilla only reopens VC01
when someone turns after a cure, so a cure for one would have left the other with no Falion.

## Known limits

- A survival mod's quest that only runs on its timers (Frostfall's) is never entered, so it stays the host's.
- An instance's aliases' scripts and its stages are not kept in the record yet (vanilla's vampire quest has neither
  that matters); a registration's time left is not kept (a single update starts its interval again).
- Dialogue conditions are judged for the picker's character (shared-dialogue.md, "The rule"), but a system quest's
  globals (`PlayerIsVampire`) are the host's there: no vanilla menu line reads it, so it is not judged per player.
- The host's NPCs see the guest's vampirism only through its copy's race: its factions are not the copy's.
- Learning is broad: any quest a player's own script calls into becomes one per player (the log names each quest
  learned, `SystemQuests: quest ... is one per player`).
- Carrying the picker through a scene its pick started (a reward after a scene going to the picker, not the host's
  player) was not needed for the cure and waits (`ideas.md`).

## Where it lives

- Rules: `PartyRules::LearnsSystemQuest`, `RouteOfSystemCall`, `RouteOf` (`InSystemQuest`, `SystemObject`).
- The list: `OwnEvents::SystemQuestList`, `Papyrus/SystemQuests` (learning, globals, the record's state).
- The hook: `GuestScripts::HookScriptCalled`; registrations from `HookNativeCalled`.
- The session: `SystemQuestService`, `HostAuthority`'s relay; the save: `CoSave` (SYSQ).
- The sync: `HostEngine` (quests and globals skipped), `GuestEngine` (not applied).
- Tests: ClientTests `system_quests_tests.cpp`, `own_events_tests.cpp`; TPTests `encoding.cpp`; GameTests
  `vampirism_flows.cpp`; Skyrim `vampirism-is-each-players`.
