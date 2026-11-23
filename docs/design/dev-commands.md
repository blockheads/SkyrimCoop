# Dev commands

Chat commands for playtests and stress tests: anyone in the party types them, the host decides the world. Built
2026-11-16. Engine path: [dev-commands.md](../re/systems/dev-commands.md).

```text
any player types /spawn wolf 3
  UiService::RunChatCommand -> DevCommands::IsDevCommand -> DevToolsService::Run
    Parse fails: the usage line in this chat, nothing sent
    alone (no session): world and player lines run here
  DevCommandRequest{Line} -> host

host: HostDevTools::OnDevCommand
  Parse again (a bad line is dropped)
  /killall:      HostGame::KillHostiles (the host's game kills what is hostile to its player)
  world command: DevCommands::WorldLines(command, ConsoleNameOf(typer's character)) -> HostGame::RunConsole
                 /spawn needs the typer's character loaded on the host (its copy), else dropped
  NotifyDevCommand{Line, By} -> every game, the typer's and the host's own too

every game: DevToolsService::OnNotifyDevCommand
  chat: "<By>: <line>"
  /killmove, /reset: IDevToolsBridge::SetKillMoveOdds
  /god, /reset: IDevToolsBridge::SetGodMode(GodAfter(command, IsGodMode()))
  DevCommands::PlayerLines(command) -> IDevToolsBridge::RunConsole, on its own player
```

## The commands

| Command | Where | Console lines |
|---|---|---|
| `/heal` | every game | `player.restoreav health|magicka|stamina 100000` |
| `/god [on\|off]` | every game | none: `PlayerCharacter::SetGodMode` (40500) from the engine's own (`IsInGodMode` 40501) |
| `/speed <0.1-10>` | every game | `player.setav speedmult <x*100>`, then carry weight +1 and -1 (the engine applies speed on the next carry weight change) |
| `/gravity <0.1-3>` | every game | `setini "fInAirFallingCharGravityMult:Havok" <1.35x>`, `setgs fJumpHeightMin <76/x>` |
| `/shouts` | every game | `player.addshout`, `teachword`, `unlockword` for the 20 player shouts' 60 words; `shoutrecoverymult 0` |
| `/gear [daedric\|glass]` | every game | `player.additem` and `equipitem` of the set (glass: bow and 50 arrows) |
| `/lootbox [1-10]` | every game | `player.additem E88 <n>` (LootBanditChestBossMagicItem100: one enchanted item each) |
| `/killmove <0-100\|default>` | every game | none: the kill move conditions' odds (below) |
| `/reset` | every game | god off, speed 1, gravity 1, kill move odds the game's own, whatever set them |
| `/spawn <kind> [1-20]` | host | `<typer>.placeatme <list> <n>`: wolf, bandit, draugr, troll, sabrecat, giant, dragon (leveled lists, Skyrim.esm) |
| `/killall` | host | none: `KillHostiles`, the console's `killall` kills every actor, copies and followers too |
| `/weather <kind>` | host | `fw <id>`; `HostWeather` shares it as any weather (no sky in an interior) |
| `/leash [on\|off]` | host | none: `HostLeash::SetOn`; off stops a running countdown (`kOff`), `/reset` turns it on |
| `/dev` | this game | the list |

`ConsoleNameOf`: the host's player is `player`, a guest's character is its copy's reference id (`FF...`). The console
takes an id as a reference only when it starts with a letter (`console.md`).

## Kill move odds

Vanilla kill moves have no chance: the kill move root idles need `ShouldAttackKill` (the swing is fatal) and, for
the player, `IsLastHostileActor` (the target is the last enemy near). So the game's own rule already plays one on most
killing blows on a lone enemy. With odds set, `KillMoveOdds` (hooks on both condition functions) answers any swing at a
victim the attacker may kill (alive, not essential, the engine's own checks before its hit probe) with a roll under the
odds, fatal or not, and the last enemy always: `/killmove 100` makes every swing a kill move, `/killmove 0` none. One
roll per attacker and target a second, as the idle tree asks many times a swing (`KillMoveRolls`). The attacker's game
decides its kill moves, so every game holds the odds.

Until 2026-11-22 the odds kept the fatal test (fatal and a roll): at 100 a kill move still came only on the killing
blow, so against one enemy at a time the rate was the game's own (`docs/re/systems/dev-commands.md`).

## Each game's own

Player commands touch only this game's player (`party-calls.md`, "a player -> that player's game"); their effects reach
the others as any change does (speed through movement, gear through equipment). Gravity and jump settings are this
game's: each game simulates its own player. Nothing is tracked: `/god` reads the engine's god mode, and `/reset` sets
every value back, so the console, the harness or a cog swap never leaves it wrong.

## Tests

ClientTests `[dev-commands]` (parse, lines, odds, console names); HostAuthorityTests `[DevTools]`; GameTests
`[dev-commands]` (a guest's `/heal` heals both players; a guest's `/spawn` gives the guest copies, `/killall` kills
them; `/killmove` and `/reset` reach both games). Skyrim 2026-11-16 (duo, by hand): spawn at the guest's copy, speed,
gravity, jump, gear, shout recovery and kill move picks read back in both games. Scenario `killmove-odds-every-swing`:
at 100 one swing at a 1000-health wolf is a kill move, the guest's and the host's.
