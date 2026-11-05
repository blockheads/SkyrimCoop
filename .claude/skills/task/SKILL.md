---
name: task
description: Start a fire-and-forget worker (own tmux window, worktree, branch) that does one prompt end to end and opens a PR to dev. Use when the user types /task <prompt>.
argument-hint: "[-a <account>] [--name <name>] <prompt>"
---

Start one worker for the prompt in the arguments with the `agent` command, then report in one line. Do not do the
work yourself.

1. **Name:** `--name` if given, else 2-3 lowercase words from the prompt joined by dashes (e.g. `torch-stays-lit`).
   It must not match a running worker (`agent status` lists them).
2. **Account:** `-a` if given. Otherwise run `agent status` and pick whichever Claude account (`agent accounts`;
   not `codex`) runs the fewest workers, `a` on a tie.
3. **Spawn:** `agent spawn <account> <name> "<prompt>\n\n<footer>"` with the footer below, verbatim.
4. **Report:** `Started <name> on <account>: <prompt, one line>.` If an orchestrator state file exists
   (`~/.agent-pool/agents-SkyrimCoop/state.md`), add the task under **Tasks** there.

Footer:

```
How to work (repo rules, CLAUDE.md):
- Step 0, first and most important: the RE in IDA. Before you reproduce or fix anything, search GitHub/Nexus for an
  existing SKSE mod, then read the engine path in IDA (ida-multi-mcp, on the instance the session start names) until you
  know the flow. Record as you read, not after: name functions and locals, set types, comment what each does (rename,
  set_comments, set_type, declare_type; not py_eval). Hooks journal every write into docs/re/journal/, so it reaches the
  repo. set_comments replaces a function's comment: keep what was there. scripts/re/id.py <ID or address> maps Address
  Library IDs to addresses. Then the short version in docs/re/systems/. Use the duo to test what the code told you, not
  instead of reading it. A fix you can't explain is not done; the PR's Engine path says what you learned.
- A bug follows the bug-fix flow (CLAUDE.md): first a scenario recreating it, played through the skyrim-coop MCP and
  record_export'ed; then a failing GameTests flow (or ClientTests/HostAuthorityTests case), the fix, and it passes.
  New behaviour gets one fast test.
- If the MCP lacks something or is awkward, extend it (a verb or tool, then mcp_reload) and use it; never script
  around it. Say in the PR which MCP tools you added or changed.
- This worktree's xmake is not configured yet: before your first build or check.sh, run scripts/xmake-linux.sh f and
  scripts/xmake-msvc.sh f -m dev.
- Builds: one target per build at -j8 (scripts/xmake-msvc.sh / scripts/xmake-linux.sh).
- The duo (two test Skyrims) is shared and locks itself: coop_test.py and the MCP's duo_start wait their turn and free
  it on exit (docs/design/testing.md, The duo lock). Use it only when the task needs Skyrim; stop your games as soon as
  you have what you need. Never ask the user about it.
- Keep code comments short; reasoning goes in docs/. Update docs/progress.md with the change.
- If you are blocked on a decision only the user can make, say so plainly and stop.
- When the work is done, ship it:
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
