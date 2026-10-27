# Shared party bounty

v1.0 feature 2. Implemented on `shared-party-bounty` from `dev` at `526cff31`, integrated with
`dev` at `9e8bbaa2`. Party rule: one player's crime counts against the party, judged by the host's
world (`party-model.md`, `copy-contract.md`).

## Authority and boundaries

- The host owns violent/nonviolent bounty, infamy and the native player-enemy flag for every crime faction. Guests propose
  actions, targets and item quantities; they never supply an accepted monetary increment.
- `HostCrime` derives the culprit from the authenticated connection's announced player character.
  It rejects invalid requests and repeated sequence numbers. Absolute snapshots synchronize host
  changes, joining, pardons and clears without adding a repeated crime twice.
- `CrimeService` mirrors snapshots into guest-native state and corrects local overwrites. Empty
  native map entries are omitted so a cleared guest faction does not trigger reconciliation forever.
- Host permissions use the party's player; witness detection uses the actual guest actor and position.
  A guest copy has a created NPC base, so directly testing its ownership against player base 7
  incorrectly marks the party's own property as trespass.
- A fine charges the chooser's personal purse once. Stolen-stack ownership is retained only in its
  own game, so a numbered, deduplicated host notification performs native confiscation there.
- Existing Games/Skyrim hooks, copy AI suppression, death/body, physics, character lifetime and
  record finalization are outside this feature. Their files are unchanged.

## Native adapters: AE 1.6.1170

All added hooks live in `CrimeBridge.cpp`; host rules and transport remain engine-independent.

| Path | Native seam and adaptation |
| --- | --- |
| Theft | StealAlarm 37422 accepts the real player after teammate resolution. The guest sends an item action; the host rechecks permission and runs the native alarm. Host activation replay on a guest copy is suppressed to avoid double charging. |
| Pickpocket | Caught-pickpocket 37423 sends a guest proposal. The native alarm handles victim/forgiveness rules; an unowned actor does not grant permission to pick its pockets. |
| Assault / murder | Host-native alarms 37425 / 37426 adapt the guest attacker to the host player for native tracking, with the guest retained in scoped detection context. Guest replay does not create another alarm. |
| Werewolf / trespass | Native alarms 37428 / 37427 use the same scoped criminal. Copies skip UpdateAI, so CrimeService separately refreshes their trespass flag/package using the NPC-safe portion of 37578; it never calls the PlayerCharacter-only tail 42676 on an NPC. |
| Actor crime views | Native NPC-body bounty getter 37379 reads the party player's bounty for guest copies. Known-crime query 41354 aliases a guest culprit to the party record, preserving victim, witness, type and day filters. Ordinary NPCs keep their own bounty and crime queries. |
| Criminal reactions | Faction computation 37667 applies the host player's native crime reaction to guests when the observer's crime faction is a player enemy or it remembers a player crime. Copies bypass knowledge caching, so adapting only cache refresh 42714 misses their direct queries. Attack check 37534 covers player-only crime branches. Both adapters chain below the existing friendly-hit hooks. Native morality, guard thresholds and surrender still decide hostility; existing guest-specific hostility is preserved. |
| Witnesses | RequestDetectionLevel 37764, detection state 37758 (used by HighestDetection 41408) and murder's direct knowledge access all call process lookup 39515. Only that shared lookup needs a hook to redirect host-player detection to the scoped guest. |
| Lockpicking | Menu open 51959 starts an attempt. Guest check 51981 asks the host. The host runs native 51981 with the proposed target and a scratch menu's reported flag (+0x10D), restoring the global target immediately. A caught attempt repeats its answer without another charge. |
| Container close | Transfer 51141 records item descriptors, extras, taking direction and ownership-stack quantities. On close, the menu's null-item StealAlarm becomes a batch proposal. The host reconstructs temporary extras and uses GetStealValue 16045; the guest's monetary accumulator is never accepted. |
| Fine | PayFine 40659 keeps PlayerCharacter state/story events on the host. PayGoldToContainer 37511 safely uses the chooser copy's TESObjectREFR inventory; existing inventory sync delivers that debit. Helper 16120 confiscates on the chooser's own game, into the faction chest at +0x70. Payment ends alarms/combat and invalidates reaction caches on all guest bodies as well as the real player. |

Native Crime stores victim handle at +0x08, criminal at +0x0C, type at +0x04 and faction at +0x60
(constructor RVA 0x7A2350). ProcessLists insertion (RVA 0x76EF30) retains only crimes attributed to
the real player. The adapter preserves that handle instead of swapping g_thePlayer or casting an
NPC body as PlayerCharacter. Guest actors nevertheless expose that same crime record through
native crime-condition queries; keeping one record does not exempt guest bodies from criminal state. KillImmediate's existing murder alarm supplies its recorded killer;
this branch changes no death code.

The bounty map is at PlayerCharacter +0x3F0: four uint32 values (violent current, nonviolent
current, nonviolent infamy, violent infamy). Getters 40641–40643 and setter virtual 0xB5 confirm
this. The bridge uses measured scatter-table layout: capacity +0x04, entries +0x20, entry size
0x20, next +0x18. The generic empty-base template and the checked-out CommonLib float layout do
not match this binary. Mirroring restores infamy after the setter's additive bookkeeping.

Container ownership quantities come from ExtraCount (signed 16-bit, default 1), not this project's
ExtraDataList::GetCount(), which counts metadata nodes. Validation rejects invalid quantities and
limits the combined number of item lines and ownership stacks to 4096 before native reconstruction.
The protocol reuses the existing inventory-item encoding; it does not redefine general inventory
serialization or its enchantment decoding.

## Guard arrest and jail

A host guard witnessing a guest's real container theft starts its native arrest force greet. The
host owns the conversation; every player's menu receives the same fine, jail and resist choices.
A guest can pay from its own purse, submit to jail, or resist and become the guard's combat target,
including while the host is invisible. No harness force greet or StartCombat supplies those reactions.

The guard tests found three shared-dialogue defects, fixed in the existing dialogue path:

- Copies have no alarm AI to select an arrest greeting. DialogueOpenedRequest / NotifyDialogueOpen
  now carry the host-selected INFO. The guest starts it through 37200's explicit force-greet branch.
  The host alone supplies this value; copies still run no topic fragments.
- Native dialogue may rebuild its choices before anyone picks. HostConversation now publishes those
  rebuilds at the current step, retaining the existing stale-pick validation.
- Building an option on a copy recalculates text tokens locally (the guard showed a 0-gold fine).
  ReplaceOptions now installs the host's rendered text with native BSString::Set (439876).

PayFine's goToJail flag moves the party to the release marker; it does not impose a sentence.
The vanilla jail choice instead calls Faction.SendPlayerToJail (55807), whose delayed functor invokes
PlayerCharacter::GoToPrison (40655, virtual 0xB9). The host runs the sentence and story effects; its
ordinary travel makes the tether bring every guest into the same locked cell. No new jail-travel
message or per-player jail teleport is needed. Native ServeTime also releases the party through
the tether. The tests check Whiterun's actual dungeon (Skyrim.esm:04A376), the host's prison faction
and positive sentence, and both players standing together. The host jail control calls the native
theft entry; guest cases steal through the actual container UI and choose with their keys.

Engine layouts formerly embedded in CrimeBridge now live in Games/Skyrim/Crime.h with checked
offsets. The AE gold-map integers differ from CommonLib's older float layout. Jail faction +0x720
and sentence +0x728 are measured in 40655. Existing death, character, Skyrim actor hooks and physics
files are unchanged.

## Sources

- [CommonLibSSE-NG TESFaction wrappers](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/main/src/RE/T/TESFaction.cpp): separate violent/nonviolent amounts; payment delegates to PlayerCharacter.
- [NoCrimeTeleportRE source](https://github.com/D7ry/NoCrimeTeleportRE/blob/main/src/main.cpp): PayFine virtual 0xBB and independent teleport/confiscation flags.
- [PlayerPayCrimeGold Tweak](https://www.nexusmods.com/skyrimspecialedition/mods/76493): payment, confiscation and teleport are distinct effects.
- [Original Skyrim manual](https://assets.ctfassets.net/rporu91m20dc/46SNPirCFGI0eC6w84UI8w/0f5b441e255a26b9371608ee71b73af3/manual_skyrim-le_x360_en-int.pdf): witnessed lockpicking and last-witness behavior.
- Inspected [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) and [SkyMP](https://github.com/skyrim-multiplayer/skymp); no reusable shared-party adjudicator found. Local native decompilations are in ignored `build/crime-re/`.

## Validation and limits

Native scenarios in `scripts/coop_crime.py` use the ordinary runner and world comparison:

| Scenario | Coverage |
| --- | --- |
| party-bounty-totals | Two holds, violent/nonviolent writes and clears, guest overwrite correction |
| guest-pays-party-fine | One guest debit, host purse unchanged, shared clear |
| guest-theft-host-witnesses | Positive host control, unseen host/visible guest, detection-off negative control |
| guest-container-theft-and-fine | Real container UI/Take All, host valuation, selective confiscation |
| guest-pickpocket-party-alarm | Guest caught-pickpocket entry, host judgment and result |
| guest-assault-party-alarm | Native assault alarm with guest culprit |
| guest-murder-party-alarm | Native murder alarm, separate victim and witness |
| guest-lockpick-party-alarm | Real guest lock menu, host caught answer, repeated-check deduplication |
| guest-trespass-state | Private-cell flag set, party-property flag cleared |
| guest-crime-flags | Zero-money enemy flags and overwrite correction; native guest bounty views; a fresh NPC naturally fights the visible guest while the host is hidden; guest payment ends hostility/combat |
| guest-guard-arrest-fine | Actual guest theft; vanilla arrest dialogue, host's price, chooser-only payment, release-marker tether and no sentence |
| guest-guard-arrest-jail | Actual guest theft; guest picks jail; native sentence and the whole party inside the jail |
| host-guard-arrest-jail | Host arrest/jail control; guest follows the native jail and release travel |
| guest-guard-arrest-resist | Actual guest theft; guest picks resist; guard naturally fights that guest with host unseen |

Current review evidence lives in ignored build/crime-re/guard-jail/:

- Native guest guard cases: resist, jail, and fine PASS (native-guards-final.log). The guest really
  steals using Take All; no injected bounty, force greet or combat supplies the result. Jail reaches
  the native sentence and both players enter the same cell. Fine payment charges only its chooser.
- The host jail control reaches the correct sentence/cell and brings the guest, but the overall
  scenario reports an existing physics lock-order inversion during release. Both symbolized stacks
  are retained in lock-order-symbolized.log: GuestPhysics::OnUpdate -> GuestEngine::FreePlaced ->
  FreeBody takes the Havok world lock then reads reference extra data; the IO cell-load job takes
  those locks in the opposite order through bhkRigidBody::SetMotionType. No checker exclusion or
  physics change was made. Native failure variants are +0xE92C16 and +0xE92E70 in the same engine function.
- An earlier host-container UI variant exposed a change-flag mismatch after jail travel: C2CE8 had
  INVENTORY (0x20) on the host but not the guest, with matching restored contents. Retained in
  native-host-fine.log; inventory/record replication was not changed to make the guard test pass.
- Fast crime/dialogue flows PASS (7 cases / 52 assertions), including a guard greeting requiring the
  host INFO, a choice-list rebuild before a pick, and one guest jail choice tethering a three-player
  party. Both lossy profiles PASS: 50 ms / 30 ms jitter / 5% loss and 120 ms / 80 ms / 10% loss
  (flows-final.log, net-1.log, net-2.log).
- Six native shared-dialogue regressions and nine of the ten earlier crime cases PASS in shuffled
  order (native-regressions.log, seed 2610272). Pickpocket's bounty checks pass, then the whole-world
  comparison finds QA ladle 32AFE rotated differently. The world checker and physics are unchanged.
- ClientTests 360 assertions, HostAuthorityTests 1647, TPTests 203, UI fixtures and the Windows plugin
  build PASS. The full gate still fails the intermittent physics carry case at physics_flows.cpp:140
  (75 units versus 45 allowed), previously reproduced on dev (baseline-carry.log). check.log retains it.

The earlier ten crime scenarios passed twice shuffled before this review (guest-flags/native-regressions.log).
The hook-chain manager also passed five isolated Windows install/chain/early-return/failure/teardown cycles
using its actual implementation (guest-flags/hook-smoke.log).

Remaining native coverage limits: trespass warning AI, werewolf transformation, last-witness removal,
Markarth's quest-specific jail, modded factions and modded guard dialogue. Assault/murder/pickpocket
fixtures invoke native alarm entries; the lock fixture uses the real menu's crime check without
turning the pins. Native jail release currently exposes the physics issue above.

## Final merge review (2026-10-26)

Reviewed action authentication/deduplication, host valuation and ownership checks, witness redirection,
chooser-only fines/confiscation, native guest criminal reactions and guard dialogue. No remaining
blocking crime/dialogue findings. Simplified container-transfer capture to return straight to native
code outside guest theft transfers. The merge preserves dev's BodyService wiring and BodyRestsRequest
opcode before appending CrimeRequest; existing actor, death and physics implementation files are unchanged.

The integrated tree passes scripts/check.sh: ClientTests 360 assertions, HostAuthorityTests 1664,
TPTests 206, GameTests 1297 / 115 cases, UI fixtures and the Windows plugin build. The earlier carry
failure did not recur in this gate; it does not establish that the native physics issues above are fixed.
Both lossy crime/dialogue profiles pass (7 cases / 52 assertions each): 50 ms / 30 ms jitter / 5% loss
and 120 ms / 80 ms / 10% loss. Logs: build/crime-final-review/check.log, net-1.log and net-2.log.
Prior native guard/arrest evidence remains the source for live
behavior; the shared duo is reserved by friendly-fire, so this merge review does not redeploy its DLL.
