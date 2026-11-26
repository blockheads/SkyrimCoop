#pragma once

#include <Services/Host/HostFollowersGame.h>
#include <Services/Host/HostCrimeGame.h>
#include <Services/Host/HostItemsGame.h>
#include <Services/Host/HostCharactersGame.h>
#include <Services/Host/HostBodiesGame.h>
#include <Services/Host/HostTimeGame.h>
#include <Services/Host/HostDevToolsGame.h>
#include <Services/Host/HostWorldGame.h>
#include <Services/Host/HostMagicGame.h>
#include <Services/Host/HostCombatGame.h>
#include <Services/Host/HostStoryGame.h>
#include <Services/Host/HostJoinGame.h>

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
