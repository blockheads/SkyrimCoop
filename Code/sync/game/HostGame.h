#pragma once

#include <followers/HostFollowersGame.h>
#include <crime/HostCrimeGame.h>
#include <items/HostItemsGame.h>
#include <characters/HostCharactersGame.h>
#include <physics/HostBodiesGame.h>
#include <time/HostTimeGame.h>
#include <tools/HostDevToolsGame.h>
#include <world/HostWorldGame.h>
#include <magic/HostMagicGame.h>
#include <combat/HostCombatGame.h>
#include <scripts/HostStoryGame.h>
#include <join/HostJoinGame.h>

/**
 * @brief The host's game as HostAuthority sees it: what it reads and changes in the engine, one part per feature.
 * The host's services provide it (HostService); the headless tests a fake.
 */
struct HostGame : HostFollowersGame,
                  HostCrimeGame,
                  HostItemsGame,
                  HostCharactersGame,
                  HostBodiesGame,
                  HostTimeGame,
                  HostDevToolsGame,
                  HostWorldGame,
                  HostMagicGame,
                  HostCombatGame,
                  HostStoryGame,
                  HostJoinGame
{
};
