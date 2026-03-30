-- Integration test: standalone fake_player binary
-- Connects to embedded GameServer as a robot second player,
-- sends scripted movements, and optionally verifies via /proc/pid/mem
target("fake_player")
    set_kind("binary")
    set_group("Tests")
    set_languages("c++20")

    add_files("fake_player.cpp")
    add_files("../mock_client.cpp")  -- reuse MockClient from plan 05-02

    add_includedirs(
        "..",                                    -- for mock_client.h
        "../../encoding",                        -- for Messages/, Structs/
        "../../server",                          -- for GameServer.h, World.h
        "../../common",                          -- for GameId, Map, etc.
        "../../components",                      -- for ConsoleRegistry
        "../../base",                            -- for base headers
        "../../admin_protocol",                  -- for AdminMessages
        "../../libraries/networking",            -- for Client.hpp, Server.hpp
        "../../native_client/game_bridge",       -- for proc_memory.h, game_reader.h
        "../../native_client",                   -- for native_client headers
        "../../external"                         -- for vendored headers
    )

    add_deps("SkyrimEncoding", "SkyrimTogetherServer")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map", "enet6", "entt",
                 "sqlite3", "lua", "sol2")

    -- Only meaningful on Linux (uses /proc/pid/mem for verification)
    if is_plat("linux") then
        add_files("../../native_client/game_bridge/proc_memory.cpp")
    end

