# Engine names: compare them case-insensitively

The engine keeps its names in one string pool (`BSFixedString`: native and script names, events, menu names,
graph events and variables). The pool is case-insensitive: pooling `GetPlayer` after `getplayer` returns
the entry already there, so a name reads back in whichever spelling was pooled first. CommonLibSSE-NG
compares `BSFixedString` with `_strnicmp` for this reason (`reference/CommonLibSSE/include/RE/B/BSFixedString.h`).

Seen 2026-10-20: `Game.GetPlayer`'s native reads back as `getplayer` on every fresh duo, so a `strcmp` with
`"GetPlayer"` never matched and the party model's rule 2 never ran (`papyrus-guest-scripts.md`, "Rule 2
never ran"). Which spelling a name gets depends on what pooled it first (a script, a plugin, the load
order), so a compare that works today can break with a mod.

## In our code

`Code/client/Games/EngineNames.h`:

- `SameName(a, b)`: equal names, whatever their case. For a name copied out of the engine into our own
  strings too (`ActionEvent::EventName`).
- `NameKey(name)`: a map key, the same for every spelling (`PapyrusService` keys the natives it calls by it).
- `BSFixedString::Is(name)` and `BSFixedString == "name"` use `SameName`. Before, `==` with a string literal
  compiled through `operator const char*` and compared pointers.

Moved onto them (2026-10-20): `GuestScripts` (`GetPlayer`, the timer natives and their events),
`PapyrusService` (a missed lookup caches a null native and crashes when called), `BSScript::Object`'s
variable lookup, `OnlyHudOpen` (`"HUD Menu"`), `FindQuestByNameId` (an editor id: a `BSString`, not pooled,
but the engine matches editor ids case-insensitively too), `KillWithDeathAction` (`"RagdollInstant"`).

Not engine names, left as they are: `GraphVariableWriters` compares `hkClass` names (literals in the
executable), `Breadcrumbs` and `HeapHistory` our own strings.
