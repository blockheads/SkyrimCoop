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
        "../../relay_dll",                       -- for protocol.h
        "../../external"                         -- for vendored headers
    )

    add_deps("SkyrimEncoding", "SkyrimTogetherServer")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map", "enet6", "entt",
                 "sqlite3", "lua", "sol2")

    -- Only meaningful on Linux (uses /proc/pid/mem for verification)
    if is_plat("linux") then
        add_files("../../native_client/game_bridge/proc_memory.cpp")
    end

-- mock_skyrim_loader: headless relay DLL test binary (MinGW/Wine only)
if is_plat("mingw") then
    target("mock_skyrim_loader")
        set_kind("binary")
        set_group("Tests")
        set_languages("c++20")
        add_files("mock_skyrim_loader.cpp")
        add_includedirs("../../relay_dll")  -- for protocol.h
        add_syslinks("ws2_32", "kernel32")
        -- Static link MinGW runtime for self-contained binary
        add_ldflags("-static-libgcc", "-static-libstdc++", "-static", "-lpthread", {force = true})
end
