# Cutscenes: the whole party sees them

Decided 2026-10-22 (the user: "yes absolutely the whole party should see cutscenes"; `party-model.md`, Cutscenes).
Research, counts and the engine's functions: `docs/research/cutscenes.md`. Step 1 built 2026-10-23.

## The screen is the party's

Vanilla has no cutscene state: a cutscene is a script taking the player over (controls, fades, the camera, a
shake, music). Decided 2026-10-23 (the user, choosing it over a cutscene gate: "people getting their game tinted is
a small issue"): **every script's call on the controls or the screen runs on every game**, each on its own player
and screen, whoever's game runs the script. So a cutscene plays for everyone, with nothing to detect.

```
any game's script: Game.DisablePlayerControls / ImageSpaceModifier.Apply / ShakeCamera / MusicType.Add ...
        |
        v
runs there, then goes to the host (RoutedCallRequest, to the others), which relays it to every other game (NotifyRoutedCall)
        |
        v
each game runs the same call on its own VM, on its own player and screen (a stack tagged kMirrored, never resent)
```

- **Which calls (no list):** a native whose own code calls one of the engine's functions for the controls
  (`ControlMap::ToggleControls`, 68545) or the screen (the fade 52847; `ImageSpaceModifierInstanceForm` 18570,
  18573, 18575, 18577; `ShakeCamera` 33012; `PlayerCamera` 50790, 50796, 50838; music 35145, 35146; the rumble
  68528), or reads the movie player (412214): `NativeReach::CallOf`, read from the code at run time. Only the
  native's own code counts: `Actor.Kill` reaches the controls deep in `Actor::SetLifeState`, `Weapon.Fire` the
  rumble through firing, and neither is the screen's. IDA reads 16 such natives in vanilla
  (`docs/research/cutscenes.md`, "The calls").
- **What a reference shows** (built 2026-10-27, `dragonborn.md`, "What every game shows"): art objects, effect
  shaders and sub-graph events (`Call::kShow`) run on every game too, on each game's copy of the reference.
- **Run as the parameter's type:** each game queues the call with every object argument typed as its parameter
  declares (`GlobalCall`). The VM rejects an `Actor` passed to an `ObjectReference` parameter, so a shake with an
  actor as its source, or an effect on an actor, used to be dropped there.
- **What it costs, accepted:** a world script's fade outside a cutscene tints everyone (a vampire in the sun,
  survival's freezing water), a shake without a source shakes everyone at full strength (one with a source fades
  with each player's distance), and a boss's music plays for everyone. A kind can be gated again if play shows it
  matters.
- **A player's own moment stays theirs** (next section): a lever, fishing, the lute, a ride or a beast form no
  longer freezes the party.
- **Each game's cutscene holds its own controls** (built 2026-10-23): the engine keeps the controls as one bitmask
  (`ControlMap::ToggleControls`, 68545, no owner or count), so two cutscenes at once used to mix: the first to give
  back gave back both. Every game now keeps who took what (`PartyRules::ControlHolds`, under `PartyNatives`' hook: its own scripts, and each other
  game's by its player id, `NotifyRoutedCall::Source`, filled in by the host). A script's give-back gives back only
  what no other holder still holds there. The engine's own toggles (menus, furniture) pass untouched.
- **Ends for sure:** a player leaving gives back, on every other game, what that player's cutscene still held there
  (`NotifyPlayerLeft`, `PartyNatives::GiveBackControls`). A game leaving the session gives back what every other
  game's cutscene held on it. The give-back is `Game.EnablePlayerControls` for what no one else holds; camera switch,
  activate and the journal (not in `ControlMap`) come back only when no one holds anything.

## Whose moment (built 2026-10-23)

A script taking the controls is not always a cutscene: a lever, fishing, a bard's lute, a carriage, a beast form
take them for one player (`docs/research/party-freezes.md`, the 81 vanilla scripts classified). Decided 2026-10-23
(the user: "let's go with this"), one rule, no lists:

```
a controls or screen call, in a stack whose cause is (GuestScripts' tags):
  no player's action (a stage or scene fragment, a quest's timer)      -> the party's
  a player's action (a guest, or this game's player 0x14)             -> that player's alone
  a player's action, run by a stage that action's scripts set (story) -> the party's
and a controls call is shared only if it asks for movement (ControlMap::ToggleControls 68545, bit 0x1):
  fighting, sneaking or menus only is a restriction, its game's alone
```

- **The cause:** GuestScripts already tags a stack with the guest whose action started it (the rules: `ScriptTags`,
  `OwnEvents`, in `ClientTests`). In a session it also tags
  this game's own player (0x14): an engine event naming the player (an activation, a trigger, a hit), the player's
  own events, SKSE's input and menu events (`WorldEvents::IsOwnEvent`, as on a guest), the host's own picked line
  (`HostConversation::LineFor`), and what those queue or register. A native's own calls keep its stack's cause.
- **The story:** a `SetStage` (`NativeReach::Call::kWorld`) in a stack with a player's cause marks what it queues (its
  fragment) as the story, and the mark follows that fragment's calls and timers (`Cause::Story`).
- **A player's moment:** on that player's game it runs there only; run on the host for a guest, it goes to the guest
  (`PlayerNatives::SendToGuest`, as other natives on the player).
- **What it misses** (`docs/research/party-freezes.md`, "Edge cases"): a cutscene a trigger starts with
  `Scene.Start` in its own stack (MG06's vision, DB09's bridge) leaves the others free; a scene a quest starts on its
  own (C02's ceremony) too. Still freezing the party: a mod's quest polling the player and taking the controls (no
  player's action behind it), a mod's stage set as bookkeeping during the player's moment. Two moments mixing (one
  bitmask) is open.
- **Everyone gets the cutscene, wherever they are** (decided 2026-10-23, the user: "no everyone gets the cutscene"):
  no distance or combat gate.

## A guest's own script changing the world

The Elder Scroll's reading is the scroll's item script, run on the reader's game: its `MQ206.SetStage(20)` would
change the quest on a guest only (decided 2026-10-22: the host's to make). On a guest, a native whose own code calls
`TESQuest::SetStage` (25004; `Quest.SetCurrentStageID`, `NativeReach::Call::kWorld`) does not run there: it goes to
the host (`RoutedCallRequest` to the host, `HostPlayerNatives::OnCall` adds the guest's character, `NotifyRoutedCall` to the
host's game), which makes the call for that guest (rule 2: its fragment runs for them) and sends the stage back as
world state (fragments skipped). The guest's script gets true at once.

Not for a quest of the guest's own (built 2026-10-23, the user: "yup I love this"): a quest the guest's own player's
scripts started on its game (its beast form, `PlayerWerewolfQuest` from the power's effect; a mod's quest its MCM
starts) is the guest's. Its stages stay there, and its events (the quest's own, its aliases') are delivered there
(`docs/design/world-state-sync.md`, "A guest's own quests").

## Scenarios

The rule and the holds are tier 1 (`ClientTests`, `party_tests.cpp`), the flows between games tier 2
(`GameTests`, `cutscene_flows.cpp`; `docs/design/testing.md`). In Skyrim:

`player-moment-stays-theirs` (a player's own script's controls and fade stay on that game, both ways),
`restriction-stays-with-game` (fighting only, from the world), `player-stage-cutscene-reaches-party` (a player's own
shake stays theirs; a stage that player's script sets is the story: `MG04MonkSceneQuest` stage 200's `PopTo`),
`cutscene-controls-reach-party` (and the client leaving mid-cutscene gets its controls back),
`cutscene-fade-reaches-party` (FadeToBlackImod and a shake), `guest-cutscene-reaches-host`,
`screen-reaches-party` (outside a cutscene), `guest-script-stage-reaches-host`, `overlapping-cutscenes-keep-controls`
(the host's cutscene and the client's own moment at once, both orders), `leaver-cutscene-gives-back` (the client leaves
mid-cutscene: the host's controls come back). A script's call is driven by the harness's
`papyrus_call` (a quest's script, no player behind it; `player_action` for one its player's action started); the screen is read by `screen` (controls, image-space
modifiers, shake). Controls: with nothing sent, the party scenarios fail at the other game's step; with no world call,
the last fails.

## Step 2: each player's own character (built 2026-10-27)

What a story does to its player's body, each game does to its own player (`party-calls.md`, "A body's move, idle or AI
drive"): `MoveTo`, `PlayIdle`, `SetPlayerAIDriven`, `SetVehicle`, `SetRestrained`, read from their code (the Body row).
An engine scene casting the player (MQ105's Greybeards) is played by each guest's own player: it runs the host's
player's scene package until the scene lets it go (`GuestScene`). A guest's own moment on the host moves its own
player there (`kCauseGame`). The mirrored move leads no party travel. Scenarios (from late saves, `--tag late-save`):
`elder-scroll-vision-reaches-party` (`LateAfterElderKnowledge`, MQ206), `greybeards-scene-casts-party`
(`LateAfterDragonRising`, MQ105 stage 270).

Not yet:

- **Camera-attached `VisualEffect`s and 2D sounds** a story plays on the player: a show (every game's) when they go
  through the show row; not checked per scene.
- **`Debug.SendAnimationEvent` on the player**: its graph event is a virtual call on its argument, which the reader
  does not follow (`docs/bugs.md`).
- **Several players at one marker** stand on one spot; checked placement beside the marker only if play shows it bad.
- **A cutscene's player leaving:** its game's own cutscene goes on there; the others get back what it held
  (built 2026-10-23). Its fades and music stay on them until its script would have ended them.
