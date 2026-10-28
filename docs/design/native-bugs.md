# Native bug fixes and evidence audit (2026-10-28)

This describes the uncommitted native-bug/barter patch on dev `4600d91a`, after the user's audit request.
It excludes the other agent's unmerged `party-scripts-2` work. Log paths below are under `build/check/`.
The pre-audit source and investigation are saved in `evidence-audit/before.tar.gz`, `before.patch` and
`pre-audit-native-bugs.md`; earlier native logs are retained.

## Disposition

Of the original 16 reports, 11 are addressed, three ragdoll/get-up reports are accepted for v1.0.0,
the original AI/quest lock report is deferred by the user, and plugin quest start/stop compatibility
is deferred with mod work. Two addressed reports were test-fixture problems, as distinguished below.
Barter Speech is implemented. Neither Experience nor Skill Uncapper was installed; mod-specific work
and compatibility claims remain outside v1.0.0.

| Report | Retained change and evidence |
|---|---|
| Story spells missed while away | Persist story grants/removals beside character records and apply them at arrival. Fast red/green: `native-bugs-spells-{red,green}.log`; native `offline-story-spells-and-personal-words`. Old co-saves remain readable. |
| Host's purchased word unlocks inherited | Clear transferred purchased flags, then restore the joining character's own unlocks. Fast `native-bugs-word-{red,green}.log`; the same native offline/rejoin scenario checks personal Fus and no inherited purchased Ro. |
| Player copies lack perks | Send owner ranks initially and when the live native list changes; apply them to the copy's unique temporary NPC base. Initial/rank/removal flows, owner-validation and encoding tests; real Mara turn-ins query HasPerk on both copies. Deferred removal is covered below. |
| Created actor alias stays host-only | Use the actor's session ID, wait for its copy, and retry unresolved fills. Fast alias flow/encoding tests; native `quest-created-actor-alias-reaches-guest`. |
| Plugin quest start/stop notifications | Deferred. Vanilla journal, stage and completion synchronization remains. The separate event relay and its additional stage-wait hook were removed in this audit. |
| Horse package after ownership change | Republish unchanged empty packages in a new ownership term; copies install their holding package directly and remove stale ExtraPackage. Fast mount regression and native `guest-rides-host-horse`. |
| AI/quest travel lock inversion | Deferred by the user after the original logs could not be located; no speculative lock change added. |
| Jail guard cleanup | Fixture correction: return to the guard's fixture cell before clearing its alarm. Inventory comparison ignores a redundant change-record flag only when actual contents match. Four Python controls; native `host-guard-arrest-jail`. |
| Copy gets up independently | Hold the copy's independent timer, replay native get-up preparation/action, and synchronize iGetUpType. Pure action-state tests and native timing checks; the delayed owner knock flag only ends resting-body correction. Remaining visual differences stay accepted. |
| Intermittent guest death pose | Keep physical body correction in native physics, with dynamic rests. Current reduced-build native death and kill-move checks pass once; the historical intermittent report remains accepted. |
| Ragdoll bone jitter | Keep precise tick subtraction, native physics ordering and reliable rests. Remove pose reconstruction described below. The front/back jitter check still fails and remains accepted. |
| Knockdown after revival | Observe the player's ragdoll transition outside the move-change funnel and wait for physics wrappers after requesting the copy's knock. Bootstrap flow and native `revived-player-can-be-knocked-down`. |
| Jail release physics lock inversion | Hold Havok, try extra data without waiting, and defer settling while busy. Retain the rest and original motion type until success. Pure/flow red-green plus native jail/travel audits; no lock-audit allowlist change. |
| Physics carry under gate load | Subtract integer ticks before float conversion; retain release barriers and reject stale held/release packets. Pure/host ordering tests, loaded carry flows and loss profiles. |
| Corpse check while fake copy alive under loss | Fixture correction: fake service timers use elapsed time under COOP_NET, and the standing member stays in combat during the corpse check. A separate real loss fix forwards action-bearing movement reliably so a dropped death action cannot stall replay. |
| Missing melee stagger | Route ProcessHit's selected stagger from the hitter's owner to the victim's owner once, independently of damage. Pure/host/flow checks and real host/guest shield bashes. |

Barter opens on the picker and only that trader earns Speech. Dialogue suspends while BarterMenu takes input.
Fast routing and host/guest/third-game flows fail before the fix and pass after it; real host/guest purchase
scenarios verify the native result.

## Removed after audit

The owner's custom model-frame/pose payload, matcher index/weights, interpolation and phase-prefetch cache
were an unsuccessful reconstruction experiment. Their unit tests checked the implementation, not native
get-up fidelity. Repeated front/back native tests still failed, so those tests did not justify retaining it.

Removed the movement payload, bridge methods, component caches and graph mutations, including hooks 59583,
58293 and 63563. Removed implementation-only tests for those deleted features. Removed the additional
render-frame sampler, graph dumps and matcher diagnostics. Existing native ragdoll, facing, timing and snap
thresholds are unchanged; the front/back reproduction and its visible-skeleton comparison remain.

Retained the body queue at 58291/58292 because native controllers otherwise overwrite main-loop corrections;
both native drive paths are covered. Bodies remain dynamic. The pending-queue property test and fake stage
flow verify ordering only; they do not prove native visual fidelity. In the reduced build all six regular
physics/death/revival scenarios pass (`evidence-audit/native-physics.log`). The added front/back check still
catches jitter (`evidence-audit/native-gameplay.log`): about 11 units of visible owner/copy difference, with
its jitter threshold exceeded. This is an accepted bug, not a reason to restore the reconstruction layer.

The separate quest start/stop relay was also beyond the native-game release requirement. The original
report concerns plugins consuming TESQuestStartStopEvent; vanilla journal completion already worked.
A native turn-in caught host stop 1 / guest stop 2 with the relay (`evidence-audit/native-gameplay.log`).
Removed the event payload/queue/replay and hook 32495 instead of adding a timing or duplicate-suppression
heuristic. Its feature-specific tests and stop-equality assertion were removed with that feature. The
vanilla journal, reward, native stage/completion and no-guest-world-fragments assertions remain.
Plugin notification parity stays recorded in [bugs.md](../bugs.md) for later compatibility work.

The audit also removed the redundant ActorExtension owner-knock shadow field and its bridge setter. Native
code read only its snapshot-known bit, not its down/up value; copies already have their owner role. The fake
used that value to invent a recovery timer instead of replaying a native action, so its timing test did not
verify the production mechanism. Removed that model and test. The body-rest flow now exercises the actual
owner-end service control, and native timing is checked in Skyrim. Movement's knock flag remains solely to
end resting-body correction on the owner's delayed timeline.

## Other audit findings

PlayerCharacter::RemovePerk (40682) queues task 88; the Papyrus progress notification can precede the mutation.
A one-shot dirty flag could read the old list and clear itself. The fake originally removed perks immediately.
A delayed-removal regression fails first (`evidence-audit/perks-red.log`) and passes after the fix
(`evidence-audit/perks-green.log`). PerkService now reads the live list and sends only changed content;
removed the connection/progress/menu dirty-flag plumbing. No guessed frame delay or new native hook.
`copy-perks-follow-native-removal` checks actual add/remove changes on both players' copies.

Character records and perk snapshots share perk collection. Damage and stagger share actor ownership rules.
Character records use the shared bounded integer reader; truncated count/length headers fail closed, older
character-only co-saves still load, and uint64 overflow is tested. No duplicate save-only integer decoder remains.

Retained native layouts and call signatures are checked against local CommonLib/Precision references and the
1.6.1170 disassembly. In particular: the physics callbacks' ABI and driver character field, BeginGetUp's
preparation/action ordering, DoDamage, the selected ProcessHit stagger call site, PutCreatedPackage's five
arguments, and the native write-lock acquisition/release. This is evidence for this engine build, not a claim
of installed-mod compatibility.

## Current validation

The final full gate passes (`evidence-audit/final-gate.log`): ClientTests 732 assertions / 80 cases,
HostAuthorityTests 1902 / 178, encoding 281 / 22, GameTests 1937 / 179 in eight shards; seven Python
comparisons, UI fixtures and the Windows DLL. Python compilation and `git diff --check` pass.

Both complete bad-line profiles pass after removing the pose payload and plugin event relay
(`evidence-audit/net-summary.log`, 1945 assertions / 180 cases each). This precedes the last shadow-state
cleanup, which changes no wire format. Both profiles also pass the final affected physics/perk flows
(`evidence-audit/final-affected-net.log`, 125 assertions / 13 cases each).

Native evidence is separated by build and purpose:

- `evidence-audit/native-physics.log`: all six regular physics, death and revival scenarios pass after removal
  of pose reconstruction. This is a control, not proof that the intermittent visual bugs are gone.
- `evidence-audit/native-gameplay.log`: barter on both players, actor alias, guest perk turn-in, native perk
  removal, both melee bashes, horse packages, physics push, jail cleanup and offline spell/word restoration pass.
  Host perk turn-in fails the plugin stop-equality check; front/back fails jitter. These failures are retained.
- `evidence-audit/final-native.log`: eight of eight checks pass after removing the plugin event relay/hook:
  both real Mara turn-ins, created actor alias, native perk removal on both players, both barter purchases,
  jail and offline spell/word restoration. The guest turn-in logs stop 0 but completion 1, stage 1 and no guest
  world fragments; plugin notification parity is explicitly deferred.
- `evidence-audit/shadow-native.log`: after removing the redundant knock shadow state, owner/copy get-up
  comparisons show no facing gaps and timing within 0-20 ms of delayed playback; the death/revive/knock sequence
  passes. The strict lying audit still fails jitter, and smoke catches a 13-unit snap for one frame at knock
  state 5->6. These remain the accepted visual reports; their thresholds were not changed.

Native tests are complete for this audit, both games are stopped and the duo is released. The original
AI/quest lock report remains deferred, and Experience/Skill Uncapper compatibility is outside v1.0.0.
Changes remain uncommitted on dev. The other agent's unmerged work still needs its own integration review.
