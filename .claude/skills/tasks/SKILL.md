---
name: tasks
description: Show every running worker in one line each (account, task or session, PR and state) and the duo's holder. Use when the user types /tasks or asks how the workers are doing.
---

1. Run `agent status` and `scripts/duo.py status`.
2. Print one line per worker: `<name> (<account>): <working | idle | waiting for you | usage limit> · PR <link and
   state (a draft is still in review, not ready for the user), or none>`. Judge the state from its last lines: a question or plan awaiting approval is "waiting for you".
   Read deeper only for a worker that looks stuck or erroring, and say what is wrong. Show a `review-<name>`
   Codex worker under its worker (`reviewing` or `posted`), not as its own line.
3. Then one line for the duo: who holds it and the queue, or `free`.
4. List workers whose PR is MERGED or CLOSED as ready for `/sync`, and any worker that hit a usage limit (suggest
   restarting it on another account). Do not close or restart anything yourself.
5. If an orchestrator state file exists (`~/.agent-pool/agents-SkyrimCoop/state.md`), bring its PR links and states
   up to date.
