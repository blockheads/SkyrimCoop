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
- Rewards: a quest's skill raise or a mod's quest experience reaches every player (experience itself
  is personal, see Experience).
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

## Experience

Decided 2026-10-19. **Experience is earned, not shared.** Players level different skills and keep their
own builds; nobody gains a skill they did not use.

In vanilla all experience comes from the player's own actions: using a skill (hits, casts, blocking,
armour, sneaking, crafting, locks, pickpockets, persuasion, trade), direct raises (skill books,
trainers, some quest rewards) and the character level, which only grows from skill raises. There is no
experience for kills, quests or discoveries, so there is no pool to share.

| What happened | Who gets it |
|---|---|
| A player used a skill (hit, cast, sneaked, crafted, picked a lock) | that player, on their own game (already so) |
| A player picked a persuade, intimidate or bribe line, or made a trade | that player's Speech |
| A player paid a trainer or read a skill book | that player |
| The party finished a quest (a skill raise, a mod's quest experience) | everyone, each on their own skills |

- **Where the host's engine acts for a peer** (a picked line's script, barter, training), the host sends
  that peer the engine's input (skill and base amount), and the peer's game adds it through its own
  `AddSkillExperience`. Never a measured result: the receiver's perks and mods then apply once.
- **Mods are not adapted.** Each game runs its own copy of every experience mod (Skill Uncapper,
  Experience, perk overhauls, Custom Skills Framework) against its own player, and reacts to events in that
  game: a quest completed, the player killed an actor, a location discovered. We make each game see what
  really happened to its own player (quest sync completes the quest on every game; the killer's game
  records its kill) and the mod does the rest. A mod breaks only where its event happens on the host's
  game alone. We never fake events for a mod (a kill credited to everyone would also raise bounties and
  satisfy quest conditions).
- **Upstream's shared combat experience is dropped** (everyone's combat gains added to the receiver's last
  used combat skill): it mixes builds, and a measured gain carries the sender's perks and mod multipliers,
  which the receiver's game applies again.

Engine path and how the mods hook it: `docs/re/systems/experience.md`.

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
- **Catch-up for a player who rarely lands hits** (a healer, a talker): none by default. A later option
  if play shows the party drifting apart in level.
