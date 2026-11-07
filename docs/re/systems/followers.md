# Vanilla follower anchors

Verified against CommonLibSSE-NG and the installed AE 1.6.1170 disassembly, 2026-10-27.
The shared-slot policy and acceptance coverage live in [the design](../../design/followers.md).
Implementation evidence is preserved in `followers-review/`; log names below use that directory.

| Engine entry | ID | Contract used |
|---|---|---|
| Actor::SetPlayerTeammate | 37717 | `void(Actor*, bool teammate, bool canDoFavor)`. Observe the successful native call; preserve the animal favor restriction. |
| ActorValueOwner::SetBaseActorValue | 38465 | `void(ActorValueOwner*, uint32_t value, float amount)`. The owner is the Actor subobject at +B8. WaitingForPlayer is actor value 95; clearing it is a Follow command even if already zero. |
| PackageTarget::resolve | 29846 | `uint32_t*(PackageTarget*, uint32_t* outputHandle, Context*)`. Context is `{TESObjectREFR* actor, TESQuest* ownerQuest}`. PackageTarget is 0x18 bytes, type at 0, target union at +8. Type 0 resolves a specific reference. |
| ContainerMenu::OpenMenu | 51140 | `void(TESObjectREFR*, int32_t mode)`. Mode 3 is follower trading. Capture that request on the host and open it through the real function on the authorized picker's game. |
| Actor::EvaluatePackage | 37401 | `void(Actor*, bool immediate, bool resetAI)`. A target change needs a procedure rebuild, subject to the current package and combat/favor restrictions. |

The follow procedure's target, read on 1.7.104 (2026-11-04):

- `PackageTarget::resolve` (29846), type 0, returns the handle stored in the target.
- `sub_14046EAD0` (unnamed, beside BGSProcedureFollow's functions; likely its start) resolves the target through the procedure's parameter, stores it in
  `BGSProcedureFollowExecState` +8 and in `AIProcess::followTarget` (+0x110, setter `sub_1406E9790`).
- 28640 (the state's update) copies +8 to `followTarget` every update, with no resolve. 28638 (its end) writes the
  invalid handle. 28641 saves +8 as a form ID.

So an unchanged state keeps its first target until the procedure restarts, which is why a new anchor needs the
rebuild below.

When the follow procedure is left with no target, and why nothing restarts it (1.7.104, 2026-11-07):

```
package pick (39114 -> CheckForNewPackageDefault 39213 -> ActorPackage_PickAndInitPackage 1406EBE40)
  picked == running package?  -> return, nothing re-initialised, no procedure restart
  else InitFromPackage 38959 (package target from 29556: type 0 reads the stored handle, no 29846)

follow start (BGSProcedureFollowExecState_Start 14046EAD0)
  target via the parameter (27770 -> 29846, our FollowerTargets hook)
  no reference for it -> procedure done (context +0x10), +8 not written

follow update (28640 copies +8 to followTarget, then by AIProcess +0x137)
  level 0/1, 28644: done if the target is dying/dead/recycled (IsDead(1)), deleted with no
                    reference handle, disabled (only with a sub-state set), or in another interior
                    cell (+63, done and failed: 1404965E0); +8 with no reference: returns, nothing done
  level 2/3, 28645: done if IsDead(0) (also essential-down), deleted or disabled

follow load (28642): +8 comes back by form id (28641 saved it); a temporary target gone by then
  leaves +8 empty, and no start runs to resolve it again
```

So a follower on 5C84B whose procedure ended, or came back from a save without its target, stands with
`followTarget` empty until something restarts the package: a package change (combat and back) or EvaluatePackage
with reset. Vanilla never meets it with the player as target; a guest's copy is a temporary actor that dies,
respawns and is not in the save. Our only restart is `FollowerTargets::Reevaluate`, on a change of the target map,
with reset only on 5C84B outside combat. Checked on the duo (2026-11-07): a party reload, a guest's death and
respawn, a follower left in another cell then moved in, a real load door and a wolf fight each ended with the
follower on the guest's current copy. Console disable and kill of a copy do not reach these paths (disabled needs a
sub-state; a copy's death is refused).

DialogueFollower (750BA) has humanoid alias 0 and animal alias 1. Recruitment already fills the host's alias
through the existing dialogue path. PlayerFollowerPackage (5C84B) still stores a reference to player 14;
the existing script context does not change that package data.

`FollowerTargets` substitutes only a specific-player target resolved for an actor in those aliases, while
its current package is 5C84B. The replacement is the member's current reference handle on the host. Other
packages and their targets use the native result. NPC ownership and guest copies keep their existing rules.

Changing the target map and calling EvaluatePackage with `(false, false)` or `(true, false)` left the old
BGSProcedureFollow target cached after a guest disconnected. The native disconnect scenario failed while
the pure selection tests passed. Rebuilding with `(true, true)` changed the live target and passed reconnect
with a replacement copy. Reset AI only while running 5C84B, with a standing target and outside combat;
EvaluatePackage itself declines while doing a favor. This cache behavior cannot be proved by the fake engine.
Evidence: `followers-cache-native.log`, `followers-restart-package-native.log`.

Opening follower inventory before the dialogue menu closed left the container visible but input went to
dialogue. The three-player GameTests flow reproduces that overlap and failed before the service closed
dialogue and waited for its close before opening inventory. The native picker-transfer scenario then passed.
Evidence: `followers-dialogue-red.log`, `followers-dialogue-green.log`,
`followers-cache-native.log`.

The implementation observes existing script cause information through GuestScripts::NativeGuest(). It does
not change Papyrus, message natives, story events or cutscenes. The hooks are engine/package/UI hooks.

The save scenario also reported a quest/alias lock inversion during fixture dismissal after loading:
`3cdb45 > 182241` versus `182e91 > 3d1507`. Both full stacks were read: native
TESQuest::ClearRefAlias -> BGSRefAlias -> ExtraAliasInstanceArray on a Papyrus worker, versus
TESObjectREFR::FinishLoadGame -> ExtraAliasInstanceArray -> TESQuest on the main thread during loading.
The checker remembers acquisition pairs across the load and later gameplay. Our frames on these paths are
the existing pass-through GuestScripts/QuestEvents and main-loop hooks, not the new target hook.

Reproduced the identical pair using unchanged dev's DLL, verified to contain neither the new follower hook
log string nor the trade authorization string. The control used the real placed Lydia, host recruitment,
save/load, then vanilla dismissal. Evidence: `followers-baseline-alias.log`, full host/guest logs
`followers-baseline-alias-host.log` and `-client.log`, and `followers-baseline-duo.log`.
It is now a reviewed load/dismissal pattern in `scripts/lock_order_known.txt`, alongside the existing
FinishLoadGame/ForceRefIntoAlias patterns. No lock implementation or script hook was changed.

Saved temporary followers exposed two ownership bugs with GameTests regressions: the host's OnConnected
was deleting its own saved temporary actors; guests could finish loading an unowned temporary actor after
connection and retain it beside the separately spawned copy. The host now retains its own actors, and a
connected guest's discovery removes unregistered temporary NPCs while preserving copies and assigned actors.
Evidence: `followers-saved-red.log`, `followers-saved-green.log`, `followers-late-red.log`,
`followers-late-green.log`, `followers-native-final.log` (saved target restored and actor audit clean; the
then-unreviewed lock pattern was the only remaining failure).

The wandering dog (WEFollowerDog, base 109487) recruits in WEDogFollowerScript.OnActivate, without a dialogue
menu. A guest's activation of a temporary NPC copy previously stopped at ToSession(localForm), which returns
no shared form ID. ActivateRequest/NotifyActivate now carry an explicit NpcId for actor targets. The host
validates the activator's owner and the host-owned NPC target, and sends the activation to that owner only.
The picker and the other copies do not replay it. The existing OnActivate argument supplies the script cause.
Evidence: `followers-activation-red.log`, `followers-activation-green.log`, `followers-activation-native.log`.

The door regression after a save reported `3cdf46 > 182241` versus `182e91 > 3d1507`.
Both full stacks in `followers-dev-native.log` show the same native ClearRefAlias path, now called inside
ForceRefIntoAlias's outer quest lock, against FinishLoadGame's reverse pair. No follower target hook is on
that path. The exact variant is reviewed alongside the direct ClearRefAlias pattern. The ordinary horse
regression's separate AI-package mismatch remains unresolved; see docs/bugs.md.
