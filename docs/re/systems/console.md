# The console

How a console line runs, and how the test harness reads what it printed (`TestHarness/ConsoleOutput.cpp`,
2026-10-21, verified in IDA).

```
Console::ExecuteCommand (442672)    echoes the line, splits it, stores each command's text in the
                                    queue (0x1431ACD78, count 0x1431ACD88; Script::sub_21883)
... later in the frame ...
Main::Update → Console::RunQueuedCommands (442673)
  per queued command: Script::CompileAndRun_Impl (441582, `(Script*, ScriptCompiler*, COMPILER_NAME 1, target ref)`)
    compile errors and natives' results → ConsoleLog::Print → ConsoleLog::VPrint (51110)
```

| ID | Function | Notes |
|---|---|---|
| 442672 | `Console::ExecuteCommand` | Queues only: nothing has run when it returns. |
| 442673 | `Console::RunQueuedCommands` | `void ()`, from `Main::Update`. Hooked by the harness (a flag while it runs). |
| 441582 | `Script::CompileAndRun_Impl` (CommonLibSSE-NG, AE 1.6.1130+) | Hooked by the harness: inside `RunQueuedCommands`, a script whose text (`Script+0x38`) is a waiting harness line takes what is printed on this thread. |
| 51110 | `ConsoleLog::VPrint` (CommonLibSSE-NG; IDA had `ConsoleLog::Print`) | `(ConsoleLog*, fmt, va_list)`: formats into `lastMessage` (`+0x001`), then appends to the history. Only caller: the variadic `ConsoleLog::Log`. Hooked by the harness. |

Natives print their results only in console mode (`TLSData::consoleMode`, TLS `+0x600`; e.g.
`GameFunc::native::GetDisabled` checks it before `ConsoleLog::Log`), which the queue's run has: a line run with
`ExecuteCommand` alone and read at once prints nothing.

## What the console prints (1.6.1170, read 2026-10-21)

| Line | Printed |
|---|---|
| `C2CE8.getitemcount 64B3D` | `GetItemCount >> 0.00` |
| `player.removeitem 64B3D 1` | `GetItemCount >> 1.00` |
| `notacommand` | `Script command "notacommand" not found.` |
| `04026B71.disable`, `1A695.getav health`, `14.getav health` | `Script command "1A695.getav" not found.`: an id starting with a digit is never taken as a reference (`C2CE8.getlocked`, `player.getav` work) |
| `C2CE8.lock 50`, `C2CE8.unlock` | `Locked All Soul Gems with lock level 50`, `Unlocked All Soul Gems `: the lock commands' own text; they do lock and unlock the reference |
| `prid C2CE8`, then `lock 50` | `PickRefByIDFunction:  (000C2CE8)`, then nothing, and the chest stays unlocked: through the harness, a command without a reference does not reach the picked one |
| `player.additem 99999999 1` | `Item '99999999' not found for parameter ObjectID.` / `Compiled script not saved!` |
| `FF00FFFF.disable` | `Syntax error.  Invalid reference 'FF00FFFF' (only object references and reference variables are allowed in this context).` |
| `player.setav` | `Missing parameter Actor Value.` / `Compiled script not saved!` |
| `coc NoSuchCell`, `player.additem 64B3D 1` | nothing |

## A line on a reference

The console takes `ID.command` only for an id starting with a letter (`C2CE8.getlocked`), and `prid` does not carry
through the harness. The harness's `console` command takes a `target`: its `CompileAndRun_Impl` hook runs that line
with the reference as the target (the argument the queue fills with the console's selected reference), and a missing
reference is reported, the line not run. `run_console` sends every `<hex id>.<command>` line that way, so scenarios
keep writing `f"{npc:X}.disable"`.

`scripts/coop_test.py`'s `run_console` waits for its line to run (harness `console` gives a ticket,
`console_result` its output), logs what it printed, and fails the scenario on `not found`, `Syntax error`,
`Missing parameter` or `Compiled script not saved`. A line that fails silently (`coc` to no cell) is still the
scenario's to check.
