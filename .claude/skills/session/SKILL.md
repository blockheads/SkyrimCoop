---
name: session
description: Start an interactive worker (own tmux window, worktree, branch) that plans with the user first, builds only after approval, and opens a PR only when told. Use when the user types /session <name> [topic].
argument-hint: "[-a <account>] <name> [topic]"
---

Start one worker that the user will drive in its own tmux window. Do not plan or build anything yourself.

1. **Name:** the first argument (lowercase, dashes). It must not match a running worker (`agent status`).
2. **Account:** `-a` if given, else the Claude account running the fewest workers (`agent status`), `a` on a tie.
3. **Spawn:** `agent spawn --no-pr <account> <name> "<topic>\n\n<footer>"` with the footer below, verbatim. With no topic,
   use `The user will describe the work when they attach.` as the topic.
4. **Report:** `Started session <name> on <account>. Attach: tmux window <name>.` If an orchestrator state file
   exists (`~/.agent-pool/agents-SkyrimCoop/state.md`), add it under **Tasks** marked as an interactive session.

Footer:

```
This is an interactive session: the user will attach to this window and work with you.
- Step 0, first and most important: the RE in IDA. Before you reproduce or fix anything, search GitHub/Nexus for an
  existing SKSE mod, then read the engine path in IDA (ida-multi-mcp, on the instance the session start names) until you
  know the flow. Record as you read, not after: name functions and locals, set types, comment what each does (rename,
  set_comments, set_type, declare_type; not py_eval). Hooks journal every write into docs/re/journal/, so it reaches the
  repo. set_comments replaces a function's comment: keep what was there. scripts/re/id.py <ID or address> maps Address
  Library IDs to addresses. Then the short version in docs/re/systems/. Use the duo to test what the code told you, not
  instead of reading it. A fix you can't explain is not done; the PR's Engine path says what you learned.
- This worktree's xmake is not configured yet: before your first build or check.sh, run scripts/xmake-linux.sh f and
  scripts/xmake-msvc.sh f -m dev.
- Start by reading what the topic needs (docs/progress.md, docs/design/, the code), then write a plan the user's
  way: ASCII flow diagrams and pseudocode per piece first, files and tests after, little prose. Then stop and wait.
- Build nothing until the user approves the plan. Discuss and revise as asked.
- When building, follow the repo rules (CLAUDE.md): the bug-fix flow for bugs, one target per build at -j8, short
  comments, docs/progress.md updated. The duo locks itself (coop_test.py / MCP duo_start wait their turn); never ask
  the user about it, and stop your games as soon as you have what you need.
- A bug's scenario is played through the skyrim-coop MCP and record_export'ed. If the MCP lacks something or is
  awkward, extend it (a verb or tool, then mcp_reload) and use it; never script around it.
- Leave the changes uncommitted for the user to review and iterate on. Never commit to or push dev.
- If the user asks for a review without shipping, do steps 1-4 below and stop, leaving the PR a draft.
- When the user says "ship it" (or similar), finish without them:
  1. scripts/check.sh passes.
  2. Commit on your agent/<name> branch, push it, and open a draft PR to dev (gh pr create --draft) saying what changed
     and how it was checked. Never commit to or push dev.
  3. Start a Codex review: agent spawn --no-pr codex review-<name> "Review draft PR #<n> in blockheads/SkyrimCoop (gh pr
     diff <n>) against the repo rules in CLAUDE.md. The RE first: does every engine claim cite an Address Library ID
     and say whether it was read in IDA, measured or inferred, and does the branch's docs/re/journal/ show the engine
     path read and named? Then correctness bugs, then rule breaks (tests for the change, the bug-fix flow, host/guest
     split, short comments). You only review: change no repo files, commit nothing, push
     nothing, run no builds. Post one review with gh pr review <n> --comment, with inline comments where useful; say
     plainly if you found nothing. Then you must run: AGENT_SESSION=agents-SkyrimCoop agent send <name> \"Review of PR #<n> posted\" and stop."
  4. Wait for that message (do nothing else meanwhile), then close the reviewer: agent done review-<name>. Read the review (gh pr view <n> --comments; gh api for inline
     comments). Fix what is right, reply on each comment with what you did or why not, scripts/check.sh again, push.
     One round only.
  5. Mark it ready: gh pr ready <n>. Then say you are done, with the PR link. The user reviews and merges it.
```
