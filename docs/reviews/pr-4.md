# PR #4 review

Reviewed [PR #4](https://github.com/blockheads/SkyrimCoop/pull/4) at
`9224345a3b057ca6922041f0fd2dcedb4f18dc71` against `CLAUDE.md`.
The [comment review](https://github.com/blockheads/SkyrimCoop/pull/4#pullrequestreview-5763593487)
was posted with `gh pr review 4 --comment`.

**P2: collision publication temporarily enables collisions between retained groups.**
In `Code/client/Games/Skyrim/PlayerCollision.cpp:80–83`, every membership change clears
all old bits before setting the new ones. Physics threads can then see two actors
that remain stacked as colliding while another actor joins or leaves. This can
reintroduce the shove the change is intended to prevent.

An isolated C++23 stress check used the reviewed `SetPassThrough`, `IsStacked`, and
`HookCompareFilterInfo` implementations. Engine group lookup was replaced with the
input ID, hook installation omitted, logging stubbed, and the original engine
filter set to return true. One thread alternated `{1,65}` and `{1,65,130}` 200,000
times; another continuously queried collision between groups 1 and 65. Those two
groups belonged to every published set.

The PR implementation returned collision 10,362,568 times in the first run,
10,099,432 in the second, and 3,301,664 in the third. The prior atomic snapshot
returned collision zero times under the same workload. These counts demonstrate
the permitted interleaving; they do not estimate its frequency in Skyrim.

Preserve retained groups throughout updates or publish a consistent snapshot.
Add a regression that exercises publication; the new fake's `SetPassThrough`
stores a vector and bypasses the production bitmap.

The other requested rule checks:

- The PR reports a live scenario reproducing the shove, a failing GameTests flow,
  unit tests, and a passing `scripts/check.sh`, matching the required bug-fix flow.
  The smoke test also teaches the fake the stepped-copy shove observed in Skyrim.
- Collision handling runs in every game, so its shared service location fits the
  host/guest split. No widened production test interface was found.
- `ArrivalStack.h:38–40` and `FakeEngine.h:951–953` have three-line source comments.
  Shorten them to the required one or two lines; measurement/history belongs in
  the existing documentation.

This review used source inspection and the isolated concurrency comparison. It
did not use the duo or independently rerun the PR's reported live acceptance.
This branch only records the review and does not change the implementation.

The review-record branch passed `scripts/check.sh`: 214 ClientTests, 199
HostAuthorityTests, 33 encoding tests, 292 GameTests, the Python and UI checks,
and the Steam 1.7.104 plugin and launcher builds. This validates the documentation
branch's baseline, rather than independently validating PR #4's implementation.
