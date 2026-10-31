# Resolved leveled NPC names on copies (2026-10-30)

A leveled NPC's resolved base can combine its original NPC's name with another NPC's face.
`TESNPC +0x1F0`, named `npcTemplate` in this project, is `faceNPC` in CommonLibSSE-NG.
`TESNPC::GetTemplateBase` follows that face ancestry. It does not describe every field of the
resolved NPC.

## Native reproduction

`leveled-spawned-npc-is-hosts` places `LvlBanditBoss` (`3DF17`) for the host. The host's runtime
base is temporary and named **Bandit Chief**. Its face base is `3DEE0`/`3DEE1`, named **Bandit
Outlaw**. The guest receives that selected face base and creates the correct face, but retains
its name, **Bandit Outlaw**.

The read-only `npc_templates` harness command captures the base's face ancestry, actual TPLT
ancestry, and `ExtraLeveledCreature` original/selected forms. `form_save` on the resolved host
base reports **change flags 0 and zero bytes**. The composite name therefore never reaches the
copy. In this reproduction there is no second random roll to explain the name difference.

Evidence (ignored, worktree `v1-copy-desync`): `build/coop-test/npc-choice-repro.log`,
`build/npc-choice-state.json` and `build/npc-choice-state.log`. The first placed-guard attempt
fails at fixture setup because the plains reference was not loaded; it is retained, and the
scenario now loads the reference's exterior cell before staging it.

## Regression and fix

The fake learns that a temporary resolved base can have a name distinct from its permanent
face base, and that native form serialization omits a name without change flag `0x20`
(CommonLibSSE-NG `TESNPC::ChangeFlags::kFullName`). GameTests `[npc-choice]` fails for both new
and placed copies: **Bandit Outlaw != Bandit Chief**. ClientTests `[npc-snapshot]` then fails
three assertions before the correction. Games are stopped and the duo released for offline work.

The announcement includes the full-name section for a temporary NPC, using the existing form
snapshot and wire layout. The copy overlays it on its selected base through the actor's ID.
Previously the created-copy path passed a **base NPC ID** to `DeserializeActorNpc`, which looks
up an **actor**, so it did nothing. Placed copies now apply the snapshot too.

The first implementation kept plugin records immutable by attaching a private NPC base.
The lifetime regression below supersedes that implementation: names now belong to the actor reference. This is the same factory/copy operation used by native
14375; its second argument is a template array, despite the unused inherited hook's guessed
`TESNPC*` signature. That hook remains disabled. The new `ExtraLeveledCreature` layout is checked
against CommonLibSSE-NG (`originalBase +0x10`, `templateBase +0x18`, size `0x20`).

Fast verification: created and placed name/race checks pass, and each game's shared face record
remains named Bandit Outlaw. Refresh and disconnect preserve placed references and clear their copy role;
a temporary private base does not make a plugin reference deletable. ClientTests: 8 assertions. Full `scripts/check.sh`: 587 cases,
6,178 assertions, Python checks, UI fixtures, plugin and launcher pass. Combined native acceptance
is recorded in the v1 issue index when completed.

## Ordinary named NPCs retain their bases

The first native acceptance run exposed an overly broad overlay: Onmund's permanent base had
save flags `0x20A`, with nonempty runtime-attribute sections and no full name. Applying those
sections to a private clone changed his identity base. Lucan's guest dialogue also failed to
open in that batch; the menu relation is checked again after narrowing the application.
Evidence: `build/coop-test/npc-identity-native.log` and `magicka-at-failure.jsonl`.

The fake now saves ordinary non-name sections and models private cloning independently of
whether a name section is present. A new `[npc-choice]` flow fails when ordinary Onmund's base
is replaced. A `[npc-snapshot]` unit fails for `0x20A`. The application policy now requires the
full-name section: ordinary runtime changes keep the named NPC's original base. No identity
comparison was relaxed to hide the private-base difference.

## Temporary bases need an explicit save mask

The first combined run retained both name failures despite the fast tests. A focused readout
shows the host's temporary Bandit Chief base still has recorded flags 0 and saves zero bytes,
after the mark request. An explicit `FormState::Save` mask `0x20` saves 14 bytes containing its
name; the permanent face saves Bandit Outlaw with that mask. Evidence:
`build/coop-test/name-flags-native.log`, `build/npc-name-flags-native.json`.

The fake's MarkActorNpcChanged now leaves temporary NPCs without a persistent change entry,
as observed. `[npc-choice]` fails four assertions (initial spawn and refresh for created and
placed references). `[npc-snapshot]` then fails two assertions for an explicit effective save
mask. Serialization now ORs the full-name section into the requested FormState mask directly;
it does not rely on marking a temporary base dirty. Ordinary NPC recorded masks and player
looks retain their existing masks. Native acceptance is rerun after the correction.

Native spawned and placed name comparisons now pass in repeated combined batches, with Bandit Chief and the host's guard identity on both games. The placed guard's overall scenario retains the travel lock-order family 17a479/e82682 versus e85437/16b61d, also recorded before this fix in the mod-profile report. It is not added to the allowlist. Evidence: `build/coop-test/name-flags-native.log`, `recovery-combined-attempt.log`, `ammo-native-combined.log`.

The completed 104-case native batch passes both complete spawned and placed name scenarios. The name mismatch is closed; the earlier travel lock report remains recorded independently. Latest gate and full combined results: [batch report](../../reports/2026-10-30-copy-desync.md).


## Native name-base lifetime regression

The completed 105-case combined batch exposes a guest crash during mounted travel. Disabling graph
capture alone or native equipment reset alone does not stop it. An isolated main-checkout DLL and
a control disabling only name overlays complete the same four-case sequence without the crash.
These controls preserve their original comparison failures.

The native allocation/free probe identifies the exact freed base. Mudcrab reference `8CACD`
receives private base `FF000F73` at `8BAE9260`; its native destructor is called at `172A20` during
`ExtraDataList::LoadGame`, case `ExtraLeveledCreature` (45). That case builds a new resolved NPC,
destroys the current temporary base and calls SetObjectReference. The actor subsequently still reads
that freed address during GC: `TESObjectREFR::Predestroy` -> GetObstacle (`19564`, `2DD84C`).
The earlier probe confirms the base is already invalid before ClearData; the actor itself still
has type62 then. Do not confuse its later corrupted crash readout with the initial cause.
Ignored evidence: `build/clone-dtor-probe-evidence` and `build/cleanup-probe-evidence`, matching DLL/PDB,
logs and canonical CrashLogger logs. Probe code is removed from shipping sources.

The fake lifetime event removes a temporary NPC while the actor still holds its base ID, matching
the observed dangling reference. `[npc-choice]` now fails four assertions: its created and placed
copies lose both a valid base and their resolved name after refresh and retirement. The name-section
unit fails before narrowing selected NPC snapshots to the full-name section; custom/player appearance
snapshots keep their other sections. All investigation, builds and tests take place with our duo stopped
and released.

The correction leaves the selected plugin NPC attached and immutable. A disposable native NPC decodes
the existing snapshot; its full name is copied into the actor reference's ExtraTextDisplayData, then
the decoder is destroyed. No actor points at the decoder. GetDisplayFullName (CommonLib 19781,
checked against native disassembly) observes the same name as Skyrim's UI. No new wire field is needed.
Native acceptance after the correction passes both name comparisons and completes mounted travel without the crash.
The final105-case batch retains the independent placed-reference lock report and a mounted player-body failure;
neither has a name difference. Canonical crash logs remain older than the final run.


## Names use native pooled equality

The next native follower-recruitment case observes Rabbit/rabbit, Wolf/wolf and Mudcrab/mudcrab.
Both TESFullName and ExtraTextDisplayData store BSFixedString (CommonLibSSE-NG headers). Native Set69165
hashes ASCII letters without case and reuses the first pooled spelling. The selected identity is the same;
re-interning its display text need not retain the same capitalization in another game.

The fake now models that pool for reference names. `[npc-name-pool]` fails the actor agreement before
using the engine's SameName equality. A Python comparison unit fails too; its separate Chief-versus-Outlaw
check continues to reject a different resolved choice. Both observers now use native case-insensitive name
equality. This does not change tolerances or ignore identity.name. See [engine names](engine-names.md).
