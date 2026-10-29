# Vampirism: each player's own

Feature 8 (`docs/progress.md`). Decided with the user, 2026-10-28:

- Vampirism, the infection and the hunger, is each player's own ("per-player makes the most sense"). It is built on
  per-player system quests: [system-quests.md](system-quests.md), whose first user it is.
- The story's cure cures every vampire, and the story's gift turns every player ("have the cure quest cure all
  vampires for now").

This replaced the shared vampirism built earlier the same day (every player turned with any one, one hunger for the
party, the host's quest's calls translated to each player's race). Its crossing from a guest's disease into the host's
quest ran the function's opening on the guest too, which showed the design deciding per native call where it should
decide per quest call.

## How vanilla turns and cures a player

```text
the disease: VampireDiseaseEffectScript (an effect on the player)
    three days, then PlayerVampireQuest.VampireChange(Game.GetPlayer())
    (a vampire's hit gives it on a roll: 10%, 15% with Dawnguard; the trap's
    TrapDiseaseSanguinareVampiris has none)
Harkon's gift: DLC1VampireTurnScript (a story quest)
    PlayerVampireQuest.VampireChange(PlayerRef)
the feed: the feed perks' fragments
    PlayerVampireQuest.VampireFeed()
the cure: QF_VC01 stage 200 (Falion, after the dawn ritual's scene), C03RampageQuest
    PlayerVampireQuest.VampireCure(Game.GetPlayer())

PlayerVampireQuest (start game enabled, no aliases, no objectives)
  VampireChange(Target): controls off, the change's art (a disabled XMarker),
    Target.SetRace(<its race>Vampire), CureRace, abilities, the hunger's clock
    (RegisterForUpdateGameTime(12)), PlayerIsVampire = 1, VC01 reopened if cured before
  OnUpdateGameTime: the hunger's stages (VampireProgression: AddSpell, RemoveSpell)
  VampireFeed: stage 1, the clock restarted
  VampireCure(Player): abilities and faction removed, the race back if it is a
    vampire's, PlayerIsVampire = 0
```

## Under system quests

The disease is the guest's own player's script, so its call into `PlayerVampireQuest` makes that quest a system quest
on every game and runs the whole of `VampireChange` on the guest's game, for the guest's player. Every flow is in
[system-quests.md](system-quests.md), "Vampirism under it".

## Not covered

- **The change's hidden marker** (`PlaceAtMe(XMarker)`, then `Disable`) is made on the guest's game only: a reference
  a guest's own script creates is that game's (`PlaceAtMe` is not a world change to the router), and the world check
  sees it on the guest alone (`bugs.md`).
- Falion's soul gem line and the innkeepers' rumor are judged for the picker's character (shared-dialogue.md, "The
  rule"), which carries its items, race, factions and added spells (the vampire's abilities, for `HasSpell`). The
  cure itself is the party's.
- Two Vampire Lords and Falion: VC01's stage 185 (`RemoveSpell(DLC1VampireChange)`) is the story's progression and
  removes the Vampire Lord power from every player, as the cure is everyone's.

## Tests

GameTests `vampirism_flows.cpp`; Skyrim `vampirism-is-each-players` (the client's trap disease turns the client and
the host's copy of it, not the host; PlayerIsVampire each game's own; VC01's stage 200 cures).
