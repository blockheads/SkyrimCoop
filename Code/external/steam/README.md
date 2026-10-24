# Steam ABI headers

Unmodified headers from ValveSoftware/GameNetworkingSockets (BSD-3, see LICENSE), commit
`d534c19aa760df3fb75fd20db13ba1932b8a5463`:
https://github.com/ValveSoftware/GameNetworkingSockets/tree/d534c19aa760df3fb75fd20db13ba1932b8a5463/include/steam

Only their data types and constants are used (`Code/client/Bridges/SteamBridge.cpp`). The plugin calls the
Steamworks flat API in Skyrim's own `steam_api64.dll`; it links neither GameNetworkingSockets nor the Steamworks SDK.
