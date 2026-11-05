---
name: quick-task
description: Start a worker for a small change that lands straight on dev, with no PR and no review. Use when the user types /quick-task <prompt>.
argument-hint: "[-a <account>] [--name <name>] <prompt>"
---

Start one worker for the prompt in the arguments with the `agent` command, then report in one line. Do not do the
work yourself. The user asked for this path: a quick task pushing to `dev` is authorized.

1. **Name:** `--name` if given, else `quick-` plus 1-3 lowercase words from the prompt, joined by dashes. It must not
   match a running worker (`agent status` lists them).
2. **Account:** `-a` if given. Otherwise run `agent status` and pick whichever Claude account (`agent accounts`;
   not `codex`) runs the fewest workers, `a` on a tie.
3. **Spawn:** `agent spawn --no-pr <account> <name> "<prompt>\n\n<footer>"` with the footer below, verbatim.
4. **Report:** `Started <name> on <account>: <prompt, one line>.` If an orchestrator state file exists
   (`~/.agent-pool/agents-SkyrimCoop/state.md`), add the task under **Tasks** there.

Footer:

```
This is a quick task: a small change that lands straight on dev, with no PR and no review.
- If it touches engine behaviour, first read the engine code in IDA (ida-multi-mcp) and record it there as you read
  (rename, set_comments, set_type: the hooks journal it into the repo), then docs/re/systems/, before changing
  anything; a fix you can't explain is not done. A docs-only change needs none.
- This worktree's xmake is not configured yet: before your first build, run scripts/xmake-linux.sh f and
  scripts/xmake-msvc.sh f -m dev (check.sh configures itself; a docs-only change needs no build).
- Do only what was asked. Keep code comments short, and add a fast test if behaviour changes.
- Stop and say so, without pushing, if it turns out to need the duo, a design decision, or more than a small change:
  the user will rerun it as /task or /session.
- To land it:
  1. Commit on your agent/<name> branch, one commit.
  2. git fetch origin && git rebase origin/dev. If a conflict's intent isn't clear, stop and say so.
  3. git push origin HEAD:dev, in the foreground with a 10-minute timeout (a fresh worktree builds
     everything, about 4 minutes): its pre-push hook runs scripts/check.sh, which is the gate (don't
     run check.sh yourself first; it skips itself when only .md files changed). If check.sh fails, fix it and push
     again; never use --no-verify. If the push is rejected because dev moved, repeat from step 2. This is the only push to dev
     you may make, and you make no other push.
  4. touch "$(git rev-parse --git-dir)/quick-task-landed", then say you are done, with the commit hash.
```
