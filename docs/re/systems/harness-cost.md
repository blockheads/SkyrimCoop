# What the test harness costs a frame: its replies' JSON

Found 2026-11-18 on the Linux duo: both games stalled ~400 ms about once a second for the first ~20 s of
`horse-passes-between-riders`, with nothing else running.

## Where the harness runs

`TestHarness::Poll` runs on the main thread from our `HookMainLoop`, the hook on `Main::Update` (ID 36564,
`ProcessEvents_Enter`, `SkyrimVM64.cpp`), before the engine's frame (sampled stack: 36544 → `HookMainLoop` →
`HookPoll`). Before this change the command server answered every request there in full: the command's engine
reads, its JSON, and `dump()`. So a slow command held the frame for as long as it took. (The draw, `Main::DrawFrame_36558`,
read in IDA on the way, is not on that path: its frames were other samples'.)

## What took the frame (measured)

- The stalls were the repro scene's world baseline (`ReproActions.scene` → `check_world`): it calls `world_state`
  with details (every reference in the loaded cells, its pose nodes, bodies, graph variables) on both games back to
  back for up to 3 s (`WORLD_WINDOW`). In Riverwood one call took 178 ms on average, 451 ms at most, on the game's
  main thread. The `Frame stalled` lines: five ~420 ms stalls 1.2 s apart on each game, the two games ~0.5 s out of step.
- Per-command counters (`harness <cmd> us`, `profile`'s Counters table) and the sampled stacks
  (`coop_test.py profile --stacks`) put most of it in building the nlohmann JSON and writing it out; the engine reads
  (`EngineCalls::GetLoadedReferences`) were the smaller part.
- Smaller, all the time: the status file (`BuildStatus().dump(2)` every 250 ms) 10.6 ms average, 80 ms at most, and
  each `status` request ~6 ms (the scene polls it ~6 times a second).

## The fix

- `CommandServer` still runs each command on the main thread (the engine is only safe there) and answers it in the
  same frame. A command with a large reply gathers instead (`Command::Gather`, `world_state`): it copies the references
  out on the main thread and builds their JSON on a worker (`ReplyQueue`, std::async); replies go out in request order.
- Replies are not all moved to the worker: `console_result` takes the line's output, and a reply held for the next
  frame met the `coc` load frame (3.6 s) after it; Python's 3 s RPC timeout closed the connection, the reply was lost
  and the scenario failed "console line never ran" (two runs of three, measured).
- The status file is dumped and written on a worker; a write still running skips the next one.
- A deferred reply whose JSON cannot be written (nlohmann throws on a non-UTF-8 name) answers as a refusal
  (`DumpReply`), as before. A client that closes while its reply is still being built leaves the queue to finish on
  its own (`CommandServer`'s closed list): Poll never waits for a worker; only Stop (unload) does.

After, same scenario: `world_state` 21 ms average, 29 ms at most on the main thread; the status file 6.5 ms, 14 ms at
most; no stall left in the scene's settle window (the remaining ones are the `coc` load and the arrival's cell loads).

A harness command that builds a large reply should gather (read on the main thread, build in the returned call);
`profile`'s `harness <cmd> us` counters show any that does not.
