# Crafted potions and poisons

Status: **built 2026-10-28** (v1 checklist, `.worktrees/crafted-potions`, uncommitted). A potion or poison a player
brews travels like any other item: to its brewer's copies, through chests, dropped, as a weapon's coating, and in the
character record.

## The problem

The alchemy menu makes a new form for each new potion (`BGSCreatedObjectManager`, an `0xFF` id). Every game numbers
its created forms itself, so the brewer's id names nothing, or something else, on another game. Before this change,
every item path read the id with `ToSession`, got nothing, and the potion was lost: from copies, chests
(`ReadContainerItems` skipped created items), drops and records. A weapon's coating with such a poison
(`ExtraPoisonId`) was lost the same way. Player-made enchantments already travelled as their effects (`EnchantData`).

## The idea: one id per recipe for the whole session

```text
brewed potion/poison here (0xFF000812)
  -> its recipe: poison?, effects (effect, magnitude, area, duration; cost kept, not part of the id)
  -> session id = GameId{Recipe::kModId 0xFFFFFFFE, FNV-1a(poison, effects sorted)}
     the same id on every game for the same potion

each game keeps a recipe book (RecipeBook, a World member): id -> recipe, in the order learned
  id -> this game's form: AddPotion / AddPoison (36167 / 36168)
     the engine reuses its potion with the same effects: one form per recipe per game
```

Two helpers replace `ToSession`/`ToGame` at the item sites (`ItemIds`, every game's code); the engine supplies only
`CreatedItems::RecipeOf` and `Make` (`AlchemyItem`; the fake engine's brewed table in GameTests):

```text
ItemId(form)  = ToSession(form), or for a player-made potion: RecipeBook::Learn(its recipe, form) -> its recipe id
ItemForm(id)  = ToGame(id), or RecipeBook::FormFor(id): the form it has while still that potion, else Make's
```

Reading and making the effects is shared with player-made enchantments: `MagicItem::SessionEffects` and `NewEffects`
(the array the created-object manager copies, freed after).

So everything keyed by item id works unchanged: player inventories and copies, container counts (`kItems`,
`kContents`, stacks), drops (`kCreate`, `MakeLooseItem`), crime lines, the purse, the host's rules, records, equality.
A coating is the same: `ExtraPoisonId = ItemId(poison)`, `SetPoison(ItemForm(id))`.

## Getting the recipe there first

```text
before any message leaves for a game, it gets the book entries it has not had
  guest: TransportService::Send   -> RecipesRequest{RecipeBook::Unsent} first (once in the session: not before
                                     the authentication); the host's NotifyRecipes are learned in HandleRecipes
                                     (sent back with the guest's own; the host keeps each once)
  host:  HostNetwork::Send(conn)  -> NotifyRecipes{Unsent(sent[conn])} first; the host's own route shares the book
  HostAuthority::OnRecipes: a guest's recipes into the host's book (HostNetwork passes them on)
```

Messages go on one ordered reliable channel, so a recipe always lands before the first message naming it. A joiner
starts at 0 and gets the whole book before anything else. Learning happens on any thread (a hook's `ItemId`); sending
only in `Send`, on the main thread.

## Rejoins

`PlayerRecord::Recipes` (written last, an older record reads none): the recipes its items and coatings name
(`RecipeBook::Of`). A guest's game learns them before it sets its inventory from the record (`JoinBridge`), so a
restarted game (no created forms, an empty book) makes its potions again.

## Engine notes

- `AddPotion` (36167) / `AddPoison` (36168): `(BGSCreatedObjectManager*, AlchemyItem** out, BSTArray<Effect>*)`,
  under the manager's lock (+0xC8); both call one worker (0x62D9D0) that walks the made potions and returns one whose
  effects match. The out pointer holds a reference, which `AlchemyItem::Make` keeps (any inventory may hold the form).
- The alchemy menu's craft is 51354 (`CraftingSubMenus::AlchemyMenu`, AE id; TrueHUD hooks its pickup message at
  +0x229). It adds the potion with `TESObjectREFR::sub_19688` (item straight into the inventory changes), not
  `AddObjectToContainer`: no copy saw a brewed potion. `Actor.cpp` hooks 19688 for actors (`ReportAdd`, shared with the
  37525 hook); `Actor::AddObjectToContainer` (37525) calls it too and reported its add already (`t_addReported`).
  19688's other callers (14864, 16127) are covered by the same hook; the cooking spit's food reaches the copy now
  (`guest-craft-raises-story` checks it).
- Effects with conditions, or a created effect, cannot be rebuilt elsewhere: `RecipeOf` gives none, the item stays
  its game's own (as before).

## Limits

- Two recipes could share a 32-bit id; the book keeps the first and logs it.
- Applying a poison changes only the weapon's extra data (no inventory event), so a copy's sword shows the coating
  from its next inventory change, as for a plugin's poison.
- Drinking a player-made potion is not replayed on the copy (its equip event names no session id), as before.

## Tests

- ClientTests `recipe_tests.cpp`: the id is order- and cost-independent, a poison or a stronger potion is another;
  the book keeps each once, gives a game what it has not had, an inventory's recipes; a form is made once and again
  when the engine let it go or reused its id.
- HostAuthorityTests `recipe_tests.cpp`: a guest's recipes are the host's and reach another game before the next
  message, once; a joiner and a returning game get the book first.
- TPTests: both messages, a record with recipes, a record from before them.
- GameTests `crafted_potion_flows.cpp` (the fake brews per game with the engine's reuse; an item or coating with no
  form here is dropped, as the engine does): a brewed potion on its copy, a chest store and the host's take, a drop
  and the host's pickup, a coated sword on the copy, a late joiner; `join_flows.cpp`: a rejoin keeps them. Five of the
  six fail with the flush switched off.
- Skyrim: `guest-brewed-potion-reaches-host` (the real alchemy menu; copy, drop, the host's pickup, the host's store
  into the guest's chest) and `guest-poison-coating-reaches-host` pass. `crafted-potion-kept-across-rejoin` passes
  every potion step (a restarted game carries the potion again) and fails only the known cover-plate pose difference
  after a main-menu rejoin, as `guest-joins-from-main-menu-as-kept-character` and `vampirism-kept-across-rejoin` do
  (on dev's DLL too).
