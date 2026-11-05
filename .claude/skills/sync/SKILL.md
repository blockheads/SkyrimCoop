---
name: sync
description: Bring dev up to date both ways (pull origin/dev into the main checkout, merge, push local dev commits), tell workers whose PRs conflict with dev to merge it, and clean up every finished worker (window, worktree and build dir removed, branch kept). Use when the user types /sync [name...].
argument-hint: "[name...]"
---

The user asked for this to run without check-ins: pulling, merging and pushing `dev` here is authorized. Work in the
main checkout (`git rev-parse --show-toplevel` must be the SkyrimCoop main checkout, not a worktree).

## 1. dev

1. `git fetch origin`. Must be on `dev`; if not, stop and say so.
2. Uncommitted changes are the user's: never stash, reset or commit them. If the merge below would touch a dirty file,
   git refuses: report the files and skip to section 2.
3. Behind `origin/dev`: `git merge --no-edit origin/dev` (a fast-forward when dev has no commits of its own).
4. On a conflict, resolve it when the intent of both sides is clear (both changes kept, docs merged, a moved line).
   Then build what the conflicted files touch (one target, -j8) and run `scripts/check.sh`. If the intent is unclear
   or check.sh fails, `git merge --abort` and report the files and why.
5. Ahead of `origin/dev` (local commits or the merge): `git push origin dev`. The pre-push hook runs check.sh; if it
   fails, report it and don't push with --no-verify.
6. One line: `dev: pulled N, pushed M` (or `up to date`), plus any conflict resolved and how.

## 2. Workers

With names given, close just those. With none, close everything that is done:

1. **Find the workers:** the folders in `~/.agent-pool/worktrees/SkyrimCoop/` plus the windows in `agent status`
   (a closed window leaves its folder behind; both count).
2. **Decide which are done:**
   - A worker is done when its PR is MERGED or CLOSED (`gh pr list --head agent/<name> --state all --json
     number,state,url`).
   - A `/quick-task` worker has no PR: it is done when `$(git -C <worktree> rev-parse --git-dir)/quick-task-landed`
     exists.
   - A `review-<name>` Codex reviewer is done when its worker is done or gone, or its review is posted and it has
     told its worker (if unsure, `agent send <name> "Review of PR #<n> posted"` before closing it).
   - Leave the rest alone: no PR yet, or an open PR.
3. **Close each done one:** `agent done <name>`. It removes the window if any, the worktree and its build dir, and
   keeps the branch. If it refuses (uncommitted changes), don't force it: list it at the end.
4. **Conflicting PRs:** for each open PR (`gh pr list --state open --json number,headRefName,mergeable`; when
   `mergeable` is `UNKNOWN`, GitHub is still computing it, so ask again once a few seconds later), if it is
   `CONFLICTING` and its worker's window is running, tell the worker:
   `agent send <name> "PR #<n> has merge conflicts with dev. Merge it, don't rebase: git fetch origin && git merge
   origin/dev. Resolve each conflict keeping what both sides meant, build what the conflicted files touch, run
   scripts/check.sh and push. If a conflict needs a decision only the user can make, say so plainly and stop.
   Otherwise reply with one line on what you resolved."`
   If `agent send` refuses because the user is inside that worker, or the worker has no window, don't start one: list
   it at the end.
5. **Report:** one line per closed worker with its PR, then the ones left open and why (`open PR #n`, `no PR`,
   `uncommitted changes`, `conflicts: told to merge` or `conflicts: not told (<why>)`).
6. If an orchestrator state file exists (`~/.agent-pool/agents-SkyrimCoop/state.md`), move the closed tasks out of
   **Tasks** into a short **Done** list with their PR links.
