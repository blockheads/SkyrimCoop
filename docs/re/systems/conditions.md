# Conditions (CTDA)

Every condition list in the game (an idle's, a perk entry's, a spell effect's, a dialogue line's, a
package's) is a `TESCondition`: a linked list of `TESConditionItem`s, each one a condition function
(`GetIsRace`, `GetActorValue`, `IsInCombat`, ... CommonLib `FUNCTION_DATA::FunctionID`) compared against a
value. Structs: `Code/client/Games/Skyrim/Components/TESCondition.h`, checked against CommonLibSSE-NG
(`RE/T/TESCondition.h`) and IDA (2026-10-14).

## One item

`TESConditionItem::IsTrue` (29924) evaluates one item alone:

- **Whom it runs on** (`runOn`, CommonLib `CONDITIONITEMOBJECT`): the subject (`actionRef` of the
  params), the target (`targetRef`), a fixed reference (`runOnRefHandle`), the subject's combat target,
  linked ref, a quest alias, package data, event data, or command target. `swapTarget` runs it on the
  target and passes the subject as the function's argument.
- **What it compares against**: `comparison.value`, or a `TESGlobal`'s current value when `flags.global`.
- **How**: `flags.opCode`, one of `==`, `!=`, `>`, `>=`, `<`, `<=`.
- **The function's own arguments**: `params[2]` (a form, an actor value index, ...; their kind depends on
  the function).

## A list

`TESCondition::IsTrue(subject, target)` (29888) builds a `ConditionCheckParams` from the two refs and
calls `sub_14049FA70`, which walks the list:

- An item with `flags.isOr` is grouped with the items after it, up to and including the first item
  without the flag. A group is true when any of its items is; once one is, the rest of the group are not
  evaluated.
- Groups are ANDed. The walk stops at the first false group.
- So OR binds tighter than AND: `A OR, B, C` is `(A || B) && C`, the Creation Kit's documented order.
- An empty list is true.
- While it walks, the params' quest, story event, `unk20` and package data list are put in the thread's
  TLS (slots 196, 200-202), where condition functions read them; they are restored after.
  `ConditionCheckParams::unk28` is cleared at the start and set on some paths (CommonLib: perk fragments
  short circuit); not traced.

## Idles

A `TESIdleForm` sits in the idle tree (`parentIdle`, `childIdles`, `prevIdle`; Creation Kit's Idle
Animations window). The engine picks an idle only when its own conditions and those of every parent up the
tree hold, for the actor performing it (subject) on its target. `animEventName` is the behaviour event it
sends (a kill move's `pa_KillMove…`).

The harness command `idle_conditions` (`{"idle", "subject", "target"}`, `EngineCalls::CheckIdleConditions`)
lists the idle and its parents (`idles`; the client's wolf kill move `0x100ECB` is `pa_KillMove1HMWolfB`
under five branch idles without an event), each with its event and `passes`: its whole list from
`TESCondition::IsTrue` (29888). Then every item of their conditions with its function, `op`, `or`,
`run_on`, comparison (a global's value when it compares against one) and its own result from 29924: which
item fails a list. An idle can play only when it and every idle above it pass. Checked 2026-10-14: each
idle's `passes` matched the grouping above applied to its items, for lists that hold and that fail. It
ruled out conditions when the client's kill move did not start (`actor-ai-and-copies.md`, "Kill moves by
the client").

Not traced: the idle picker itself (which walks the tree and tests these), and what `unk20` holds.

## Judged for a character (2026-10-28)

`JudgedFor::Scope` (thread-local) makes the conditions evaluated on its thread judge a guest's character as the player:
the story manager's events for a character (rule 4) and each player's dialogue lines. The hook on
`TESConditionItem::IsTrue` (29924) passes every item through untouched outside a scope; inside one, the params'
subject or target that is the player becomes the character, and an item reading the player another way is evaluated
from a stack copy (`JudgedRules`):

- run on a fixed reference that is the player, or a quest alias holding it: run on the character (`runOn` 2, its
  handle);
- a parameter that is the player: the character;
- `GetPCIsClass/IsRace/IsSex/InFaction` (129 to 132), which read `g_thePlayer` themselves: their own function
  (68 to 71) on the character;
- `GetIntimidateSuccess` (655), `GetBribeSuccess` (654): worked out for the character (`dialogue.md`, "Judging a line
  for another player"), compared by the item's opCode.

`IsTrue` is on every condition's stack, on every thread: `LockOrder::IgnoreFrames` covers its pass-through, so an engine
lock order seen there is not ours unless a judgement runs (`JudgedIsTrue`).
