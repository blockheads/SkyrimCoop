#pragma once

// A downed player's spectator cycle (left/right): the next standing player (+1) or the previous (-1).
struct SpectateCycleEvent
{
    int Direction = 1;
};

// This game's player is back from a real death (PlayerCharacter::Revive): its copies are made again, alive.
struct PlayerRevivedEvent
{
};

// A script took or gave back the player's controls: the spectator choice is looked at again.
struct SpectatorRefreshEvent
{
};
