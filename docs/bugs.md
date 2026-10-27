# Known bugs (after v1.0.0)

Bugs we have seen and chose to ship with in 1.0. Each entry: what a player sees, the evidence, where to start, and the
test that shows it. Fixing one: a test in the fastest tier that can show it first (CLAUDE.md), then the fix, then
delete the entry and say where the evidence went. Unfinished work and unexplained behaviour stay in
`docs/progress.md` ("Loose ends"); ideas in `docs/ideas.md`.

## Physics and ragdolls

- **A knocked-down copy gets up on its own timing** (2026-10-26, friendly-fire). The copy's get-up is its own
  engine's knock update, not its owner's: it gets up up to 1.6 s early or (once) 7 s late, and now and then plays a
  different get-up, facing 40-110 degrees away from its owner's for up to 2 s.
  - Seen: `knockdown-smoke` (Lydia knocked down three times between the players) fails about one run in five; its
    `compare_report` lists each get-up's time on both games and the facing gaps.
  - Related: Skyrim pops a getting-up skeleton onto its reference for one frame at knock state 6, owners too (up to
    80 units). A copy getting up apart from its owner shows the pop at another time, and the smoke counts it as the
    copy's own.
  - Start: make the copy get up when its owner does (its owner's knock state 5 replayed, or the copy's knock update
    held until then), with a GameTests flow in which the fake copy would get up first. Notes:
    `docs/design/physics-sync.md`, "A copy's ragdoll, steered" (open items).
- **`death-spectate-guest` failed once after the ragdoll steering work** (2026-10-26). The host's copy of the dead
  guest lay 16 units from the guest's body, 3 bones up to 17 units off their ragdoll bodies. It passed at 16:49 that
  day, before the physics changes; not run again or looked into.
  - Start: run it 3 times; if it fails, compare a `RagdollTrace` of the guest's body and its copy (as
    `knockdown-lies-as-host` does). Suspects: steering (`BodyBridge::Drive`), and copies never placed from a lying point
    (`CharacterService::RunRemoteUpdates`, `Movement::Lying`).
- **A knocked-down copy's bones still shake a little** (2026-10-26). `knockdown-lies-as-host` fails its jitter check:
  a bone turned back 4 times within a second, 8 flickers, a bone 35 units off its body (before the steering work: 9
  to 12 turn-backs a second). `kill-move-kills-guest` fails on the same check.
  - Start: the trace's per-bone series (`ragdoll.json` under `build/coop-test/traces/`), steering gain
    (`BodyPrediction::kSteerSeconds`), and whether the shaking bones are the ones the copy's ragdoll driver powers.
- **A revived player knocked down later stays getting up** (knock state 6), and its copies never go down. On dev
  before friendly fire too. `docs/design/death.md`, "The ragdoll" (open).
- **The host's jail release reported a lock inversion** (2026-10-26, shared-party-bounty): GuestPhysics
  `FreePlaced`/`FreeBody` during the release, symbolized in that worktree's `build/crime-re/guard-jail/`.
- **A physics carry check fails in the full gate** (seen by shared-party-bounty, 2026-10-26, "baseline physics carry
  failure"), and pickpocketing ended once with the QA ladles at another pose on the guest. Not traced.
  - Measured 2026-10-26 (dropped-items): `physics_flows.cpp:147` ("A guest's player carries the plate and lets go"),
    the host's plate 75 units from the guest's where 20 is allowed, in about 3 of 8 gate-style runs (8 shards at once)
    with dev's binary and with dropped-items' alike; 5 of 5 alone. Under `net_check.sh` both profiles also fail
    `physics_flows.cpp:279` (a knocked-down NPC's copy's bones) and abort in `:354` (a guest's player's ragdoll), on dev
    too.
  - Start: the flow under CPU load (the gate's shards); its timing assumptions against `BodyPlayback`'s 100 ms.

## Combat

- **Melee stagger is not sent.** A power bash on a player (or a host NPC's copy) hurts but does not stagger it; only
  the hit's damage reaches the victim's game. `docs/design/friendly-fire.md`, "Melee stagger".
