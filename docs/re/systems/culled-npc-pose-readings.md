# Culled NPC pose readings (2026-10-30)

The combined copy-desync run repeatedly reported Lydia's owner head81–88 versus her copy122,
although both recorded JumpLand and their saved state machines agreed. This is a stale skeleton
reading on the off-screen owner, not evidence that the copy refused that action.

Native controls, with the original failures preserved:

- Owner and copy GetScale both1.00, base1.03.
- Reloading the owner's existing graph snapshot into the copy succeeds (202–222us), but its
  head remains122. Reloading alone does not correct this reading.
- Turning only the owner's camera toward Lydia changes its head88.08→124.12 and pelvis52.84→71.12.
  Its recorded JumpLand/action/idle/state flags do not change. The copy remains near122/69,
  within the existing20-unit pose tolerance.
- The owner still has `process_me=true`; the earlier frozen-owner hypothesis does not explain this case.

Evidence is ignored: `build/coop-test/pose-reload-native.log`, `pose-camera-native.log`,
`build/pose-reload-probe.json`, and the exact per-comparison samples in
`build/coop-test/desync-samples.jsonl`. Probe loads/camera changes happen only after preserving
the original failed comparison; they never convert that run into a pass.

The fake now models the distinction: an animation can finish while an off-screen skeleton
retains its last drawn pose. `[culled-pose]` reproduces the same88/124 false desync and fails
before the observer correction. Its camera-visible step exposes the current standing pose.
The comparison unit suite fails three tests first; visible pose mismatches, player body checks,
and animation differences on off-screen NPCs are explicitly retained.

The observer now compares NPC bone poses only when both are visible. It still compares all
other actor properties, including off-screen animation markers, names, pools and equipment.
Player body checks retain their previous scope. Missing visibility metadata retains the old
comparison behavior. No tolerance increases or animation allowlists are introduced.

Read-only harness metadata uses CommonLibSSE-NG's `Actor::BOOL_BITS::kWasInFrustum` (bit21),
plus the low culling nibble read by Precision's HighActorCuller hook and `lastSeenTime`.
Sources: `reference/CommonLibSSE-NG/include/RE/A/Actor.h` and
`reference/plugins/Precision/src/Hooks.cpp`. The AE actor culling field is typed at27C and
last-seen field264, both offset-asserted. The final-build calibration confirms the mapping: owner head83.24 has cull6/frustum=false,
copy head122.40 has cull7/frustum=true; the qualified comparison is empty and dialogue-peer-starts passes
(`build/pose-visibility-acceptance-sample.json`). No gameplay hook or bridge interface is added.

The gait sampler also reads skeleton nodes; its earlier off-screen results need visible-sample
controls before diagnosing sliding. This change does not declare those gait results fixed.

## 2026-11-05: `was_in_frustum` goes stale

`kWasInFrustum` is written only by `Actor::ProcessTracking` (38009), as cull nibble == 7, and that function is gated
off for many actors (`Actor::UpdateAnimation` 37361), so the bit can read true for an actor culled for seconds
(Dorthe: cull 5, bit true, foot bones still for 5 s). The engine's own test for posing a graph is the 3D's
`kNotVisible` (1 << 20), set by `HighActorCuller::CullActor` (40335) and read by `Actor::ModifyAnimationUpdateData`
(38054). The harness now reports it as `pose_visibility.drawn`; the observer and the gait check use it. The gait
results above are now controlled: `GaitWatch` counts drawn steps only (`actor-movement.md`, "A culled actor keeps its
pose").

## 2026-11-10: a player's copy is culled like any actor

`teleport-keeps-copies` failed with players on the graph input stream: the guest's player's copy on the host read head
81 against its owner's 124, for seconds, after a landing. Measured on a failing run:

- its graph was its owner's on every update compared (`graph_stream_check`: 524 of 524);
- the copy read cull 6, not in the frustum, `pose_visibility.drawn` false for the whole 3.7 s (44 samples);
- the host follows a guest's teleport (the party tether) and the copy is undrawn for about half a second around every
  one; a landing that falls inside it leaves the crouch on the bones until the copy is drawn again.

So the observer skips a player's pose fields too when its copy is not drawn (`comparisons.py`; `test_world_diff.py`,
`PoseVisibility`). The owner's side is not asked for a player: its own game reads its body as it stands. With
players off the stream the same scenario passed 18 of 18: not explained (a replayed action lands a few frames later
than the stream does).
