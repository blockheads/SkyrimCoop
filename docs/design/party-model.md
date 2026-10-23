# Party model: the party is the Dragonborn

How a single-player game's story becomes a co-op one. Agreed 2026-10-13 and 2026-10-14; the systems that
build on it (shared dialogue, quests, cutscenes) refer here.

Skyrim and every mod are written for one player: scripts hold a bound `PlayerRef` and compare against it,
conditions test "the player". Patching scripts one by one never scales to mods. Instead the host's script
engine is taught, once, that every party member is the player (below: "A guest's actions on the host");
no script or mod is patched.

**The party is the Dragonborn.** The host's player is the party's one avatar in the engine. In the user's
words: "It matters not who interacts with things because it simply just triggers as if the host does it."

## One rule

**Whatever the world does to "the player", it does to the party.** What the party shares (below) is
mirrored to every member whoever triggered it. What only a body can take (being aimed at, moved, transformed,
a camera effect), and items and gold (each player's own), go to the member whose action started the script. Knowing who that is was once rejected
as breaking at the first timer; the 2026-10-19 spikes showed it follows timers and follow-up scripts
(`docs/research/player-events.md`).

Shared by the party:

- Rewards: a quest's skill raise or a mod's quest experience reaches every player (experience itself
  is personal, see Experience).
- Words of power (dragon souls likely too, since they unlock words).
- Identity changes: beast blood, vampirism, titles (Thane, Harbinger, Arch-Mage), marriage.

## The clean line

| Who acts | Applies to | How |
|---|---|---|
| The world (quests, scripts, mods → "the player") | the party, for what it shares; the member whose action started it, for their body | run on the host; shared results mirrored to everyone |
| The engine on a body (hits, spell effects, diseases, equipping, using a skill) | that actor | goes to the actor's owner (`copy-contract.md`), as today |

A player owns only their body: appearance, what they wear and wield, health, magicka and stamina, and the
skills and perks earned by using them.

## Who runs what

- **The host's game runs the story**: every quest, its stages and fragments, the Story Manager, NPC AI.
- **Peers' games run no quest logic.** They show the host's journal, quest popups and map markers.
- **A peer's interactions reach the host's engine through its copy**, which the host's scripts treat as the
  player (next section): the copy activates, enters triggers and kills on the host; the peer's story events
  (location changes, crafting) are raised on the host for the copy.
- **Characters live on the host** (`join-flow.md`, host-stored characters). Leaving is disconnecting.

## A guest's actions on the host (decided 2026-10-19)

Generic: rules in the host's script engine and story manager, never per script or per mod. Evidence and
numbers: `docs/research/player-events.md` (spikes, "Script scan"). Built: rules 1 and 2
(`Code/client/Games/Skyrim/Papyrus/GuestScripts.cpp`, `docs/re/systems/papyrus-guest-scripts.md`; scenarios
`plate-counts-client`, `sword-box-asks-client`, `statue-fires-at-client`).

1. **Every party member is the player.** On the host, any script comparison with the player
   (`== Game.GetPlayer()`, `== PlayerRef`, a player stored in a variable) is true for a remote player's
   copy. Host only, guest players only. 75% of vanilla player checks after a player's action need nothing
   more.
2. **"The player" is the member whose action started the script.** In a script the host runs because of a
   member's action (an event with their copy as its actor: activate, trigger, hit, spell, death),
   `Game.GetPlayer()` returns that member's copy. So does every script it starts in turn: an `Activate` it
   calls, a quest fragment from its `SetStage`, the `OnUpdate` it registers. Aimed traps hit whoever walked
   in; a transformation, a camera effect, a `MoveTo` reach that member's body (and through the copy, its
   owner). A member's picked dialogue line is their action too (2026-10-22). A player the script stored (a
   `PlayerRef` property, a variable set at load) is that member too, as `self` or an argument of a native. Natives
   that act on this player without naming them (skills, stats, perk points, achievements, the training and race
   menus) run on that member's game instead, and one returning a value (`QueryStat`) answers from there; the screen's
   (controls, fades, camera) wait for the cutscene design (`docs/re/systems/papyrus-guest-scripts.md`, the table).
3. **With no member's action behind it, position questions ask about the party.** Timers and loads that ask
   where the player is (`GetDistance`, `IsInLocation`, `GetCurrentLocation`, about 15 engine functions):
   true if any member is, the nearest member for a distance. Anything else without a cause is the host's
   player, mirrored to the party for what it shares.
4. **A member's story events are judged for that member.** A peer's location change (or craft, or kill) is
   raised on the host for its copy, and the story manager tests its conditions against that copy, not the
   host's player (its node and quest visitors, 32429 and 32430).
5. **Messages go to everyone:** `Debug.Notification`, `Debug.MessageBox`, `Message.Show` on the host are
   shown on every game.

**Required for it to work fully: a peer's own world scripts stop changing the world.** A peer's game also
runs the world's scripts for its own player today, so a lever or trigger fires on both games and the
effects happen twice. Only the host decides (`docs/design/world-state-sync.md`): a peer's game still raises
its player's events, for the host, but its world scripts must not commit world changes. Its own piece of
work, in world-state sync.

Measured, not yet ruled: engine code (not scripts) that checks the player while a copy acts on the host
(55 functions in melee, spells, kills and a trigger: kill credit, crime, faction reaction, detection).
Default: treat the copy as the player; each checked for player-only code paths.

## Items and gold: each player's own

Decided 2026-10-22, replacing one shared inventory and purse (2026-10-19). Each player keeps their own
inventory and gold, as on their own game today. Items a script gives or takes follow rule 2 (below, "A guest's
actions on the host"): `Game.GetPlayer()` in a script a guest's action started is that guest's copy, which
carries its player's inventory on the host, and an `AddItem` or `RemoveItem` on it reaches that player
(`sword-box-asks-client`). The copy follows its player's changes as they are made: `TESObjectREFR::RemoveItem` and
`AddObjectToContainer`, and the removals the engine makes on the inventory itself (`InventoryChanges::RemoveItem`,
16059: the Training Menu's and a fine's gold through `PlayerCharacter::PayGoldToContainer`, 37511; a trapped soul's
gem), which skipped the first left the copy richer by every lesson paid (`dialogue-picker-trains`, 2026-10-22).

Why not shared: every item change on every game (loot, drops, crafting, food, arrows, trade) would go through
the host with its own data (tempering, enchantment, charge), two players wearing one piece would need claims on
a shared stack, and carry weight would be one. Personal loot is also what co-op players expect. Shared, scripts
and conditions would have been right whoever acted; with each player's own, three gaps stay:

- **Conditions are the host's player's.** The host's engine builds a dialogue list for its player (shared
  dialogue's rule), so an item or gold condition asks the host's inventory: a guest holding Lucan's claw sees
  no option to hand it in. The guest gives it to the host, or the host talks.
- **Items with no player's action behind them** (a timer, a stage set by the world) go to the host's player
  (rule 3), who shares them out.
- **What a guest's script gives the player to own is the party's** (decided 2026-10-22, the user: "exactly right").
  `RentRoomScript.RentRoom` gives the bed to `Game.GetPlayer().GetActorBase()`, a stable sells a horse the same way.
  For a guest that is its copy's base, a created form, which the engine refuses as an owner
  (`ExtraDataList::SetOwnerForm`, 11609). On the host, the hook on 11609 writes such an owner as the player's base
  (0x7), which already means each game's own player (the host's ownership travels that way): the room or horse is
  everyone's (`GuestScripts.cpp`; checked in `dialogue-pick-says-the-hosts-line`). Rejected: keeping "owned by player
  S" beside the engine for S's game alone, unless play shows players wanting their own rooms or horses.

A picked line's fragments run for its picker (rule 2 for dialogue, built 2026-10-22: `shared-dialogue.md`, What
players see). To build: giving items between players. A shared purse comes back only if play shows sharing gold out is a
chore.

## Dialogue

One shared conversation, run by the host's engine whoever started it. Everyone sees it in the engine's
dialogue menu, each with their own options: the host's topics with that player's own lines (their factions,
items, perks, race, Speech), and their own topics. Anyone picks and speaks for the party with their own line;
the chat names who said what (2026-10-19). A picked line's script runs for the picker (rule 2): its items and gold,
its Speech experience and its training menu are the picker's; barter, gifts and a follower's inventory should be
too (later).
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

- **Engine checks for a copy:** the 55 measured functions (previous section), and crime, dialogue and item
  paths not measured yet.

- **Race changes are translated, not copied:** vampirism turns the host's Nord into `NordRaceVampire`; a
  Khajiit peer gets the vampire version of their own race.
- **Diseases cross the line:** Sanguinare Vampiris lands on a body (a peer's), but turning is a quest that
  watches the host's player, so a peer's infection never progresses. Either the host's infection turns the
  party, or a body effect on any member counts as the party's.
- **Catch-up for a player who rarely lands hits** (a healer, a talker): none by default. A later option
  if play shows the party drifting apart in level.
