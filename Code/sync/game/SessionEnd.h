#pragma once

struct World;

// Ending this game's session, whatever part it played: the host announces the end and stops hosting, the transport
// closes, Steam's lobby is left, a join under way is cancelled, and the game goes back to the main menu.
// SessionService decides when (its End and QuitToMainMenu); this is what it then runs, set once as the game is built.
void WireSessionEnd(World& aWorld) noexcept;
