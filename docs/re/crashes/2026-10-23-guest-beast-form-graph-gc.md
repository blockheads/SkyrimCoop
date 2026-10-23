# 2026-10-23: host crash freeing a behaviour graph, a guest's beast form (cause removed; the crash itself not investigated)

**Symptom:** the host crashed in `client-beast-form-stays-client`, in Havok's `hkThreadMemory::blockFree` (80588)
from `hkbBehaviorGraph` (58326) under `BShkbAnimationGraph::dtor` (63758) and `BSAnimationGraphManager::dtor`
(63411), on a `Job_Garbage` thread (39077): an animation graph manager freed with a bad block pointer. Crash log
`prefix-host/.../SKSE/crash-2026-10-23-09-25-52.log`.

**What ran just before (host log):** `HostService: Quest.SetCurrentStageID from guest FF00082D's own script runs here,
for it`, then `ImageSpaceModifier.Apply goes to the party`. The guest's own werewolf script sets a
`PlayerWerewolfQuest` stage; step 1's world calls (`PlayerNatives::SendToHost`, `kWorld`) send it to the host, which
runs the beast form's fragment for the guest (rule 2: the guest's copy is the player there, so the copy changes race on
the host). A second run, with no crash, failed at "[client] the client is a werewolf": the guest's own fragment never
runs, as its stage is set on the host.

**Likely:** a guest's per-player system quest (beast form, vampire) is not "the world": its stage should stay on the
guest's game. A design question for world calls (`docs/design/cutscenes.md`, "A guest's own script changing the
world"). The graph crash itself is not looked into (2026-10-23, the user: "just note down any crashes for now").

**Since (2026-10-23):** a quest the guest's own scripts start is the guest's, its stages stay there
(`docs/design/world-state-sync.md`, "A guest's own quests"): the host no longer changes the guest's character, and
`client-beast-form-stays-client` passes. Why changing a copy's race on the host crashed the graph's garbage
collection is still unknown; a host script changing a guest's race (a vampire's cure, a mod) could hit it again.
