# Party model: the party is the Dragonborn

How a single-player game's story becomes a co-op one. Agreed 2026-10-13 and 2026-10-14; the systems that
build on it (shared dialogue, quests, the shared purse, cutscenes) refer here.

Skyrim and every mod are written for one player: scripts hold a bound `PlayerRef` and compare against it,
conditions test "the player". Patching scripts one by one never scales to mods, and making a copy count as
the player inside comparisons cannot work (the script already holds the handle). So we do not try to make
the engine see several players.

**The party is the Dragonborn.** The host's player is the party's one avatar in the engine. In the user's
words: "It matters not who interacts with things because it simply just triggers as if the host does it."

## One rule

**Whatever the world does to "the player", it does to the party.** Quests, scripts and mods act on the
host's `PlayerRef`; the host's engine applies it to the host's player, and it is mirrored to every party
member. Nobody records who triggered what: a per-player list of "identity" effects that would need it was
rejected as an outlier (it breaks at the first timer or deferred script).

Shared by the party:

- Gold: one purse, the host's.
- Experience.
- Quest items: every player holds them.
- Words of power (dragon souls likely too, since they unlock words).
- Identity changes: beast blood, vampirism, titles (Thane, Harbinger, Arch-Mage), marriage.

## The clean line

| Who acts | Applies to | How |
|---|---|---|
| The world (quests, scripts, mods → `PlayerRef`) | the party | run on the host's player, mirrored to everyone |
| The engine on a body (hits, spell effects, diseases, equipping, using a skill) | that actor | goes to the actor's owner (`copy-contract.md`), as today |

A player owns only their body and kit: appearance, equipment, regular loot, health, magicka and stamina,
and the skills and perks earned by using them.

## Who runs what

- **The host's game runs the story**: every quest, its stages and fragments, the Story Manager, NPC AI.
- **Peers' games run no quest logic.** They show the host's journal, quest popups and map markers.
- **A peer's interactions reach the host's engine as the player's**: an activation as
  `ref.Activate(PlayerRef)`, a kill as the player's Story Manager kill event, a trigger entry as
  `OnTriggerEnter(PlayerRef)`, a picked-up quest item into the purse's inventory. That is a few engine
  sites (Papyrus event dispatch, Story Manager event sends, the activate and dialogue paths), not one patch
  per mod. First step before engine work: a scanner over Papyrus sources and plugin conditions (vanilla and
  a few popular quest mods) counting the player checks, to measure how much this covers.
- **Characters live on the host** (`join-flow.md`, host-stored characters). Leaving is disconnecting.

## Dialogue

One shared conversation, run by the host's engine whoever started it. Everyone sees it in the engine's
dialogue menu; anyone moves the cursor and anyone picks; each player's own options (their factions, items,
perks, race, Speech) show alongside the host's. Barter picked from it is shared: everyone trades at once.
Details: `shared-dialogue.md`.

Decided 2026-10-14. It replaces the 2026-10-13 suggestion (whoever started the conversation picks, the
others suggest; checks on the host's stats).

## Cutscenes

Cutscenes play for everyone as in single player, for any mod, whoever triggered them.

- The trigger is generic, the host's player's state: controls disabled, AI-driven, a scene actor, or in
  forced furniture. That puts every game in cutscene mode.
- **Each player sees their own character in it** (decided 2026-10-14). What the host's player does in the
  scene, each peer's own player does on that peer's game: the same package, furniture, `MoveTo`, controls,
  camera, fades, imagespace, music and shake.
- A scene with room for one player only (Helgen's cart seat) gets its rule with the cutscene design.

## Still open

- **Race changes are translated, not copied:** vampirism turns the host's Nord into `NordRaceVampire`; a
  Khajiit peer gets the vampire version of their own race.
- **Diseases cross the line:** Sanguinare Vampiris lands on a body (a peer's), but turning is a quest that
  watches the host's player, so a peer's infection never progresses. Either the host's infection turns the
  party, or a body effect on any member counts as the party's.
- **Experience's rule:** today only combat-skill XP is shared, added to the receiver's last used combat
  skill (upstream's rule, `shared-xp`). Quest XP and the rule itself are to decide.
