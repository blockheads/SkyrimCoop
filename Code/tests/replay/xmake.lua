target("ReplayTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx20")

    -- Test sources
    add_files("*.cpp")

    -- ReplayHarness
    add_files("../../native_client/replay/*.cpp")

    -- MessageSerializer (Recording)
    add_files("../../server/Recording/*.cpp")

    -- Shared dispatch function
    add_files("../../native_client/dispatch_hook_event.cpp")

    -- All 8 services + systems
    add_files("../../native_client/Services/*.cpp")
    add_files("../../native_client/Systems/*.cpp")

    -- Game bridge (PointerTable, TcpClient, ProcMemReader, GameReader)
    add_files("../../native_client/game_bridge/pointer_table.cpp")
    add_files("../../native_client/game_bridge/tcp_client.cpp")
    add_files("../../native_client/game_bridge/game_reader.cpp")
    if is_plat("linux") then
        add_files("../../native_client/game_bridge/proc_memory.cpp")
    end

    -- Transport (LoopbackTransport, ServerMessageRouter)
    add_files("../../native_client/transport/*.cpp")
    add_includedirs("../../native_client/transport")

    -- Include paths (mirror SkyrimCoopNative + TPTests)
    add_includedirs("../../native_client")
    add_includedirs("../../relay_dll")
    add_includedirs("../../native_client/game_bridge")
    add_includedirs("../../server")
    add_includedirs("../../common")
    add_includedirs("../../components")
    add_includedirs("../../base")
    add_includedirs("../../admin_protocol")
    add_includedirs("../../libraries/networking")
    add_includedirs("../../external")
    add_includedirs("../../encoding")
    add_includedirs("../../client/Games/Skyrim")

    -- FIXTURE_PATH macro: absolute path to test fixtures (relative to project root)
    add_defines("FIXTURE_PATH=\"" .. path.absolute(path.join(os.projectdir(), "tests/fixtures/replay")) .. "\"")

    -- Dependencies
    add_deps("SkyrimEncoding", "SkyrimTogetherServer")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map", "rpmalloc",
                 "enet6", "entt", "nlohmann_json", "sqlite3", "lua", "sol2")

    -- Match MSVC struct layout for game struct headers
    add_cxflags("-mms-bitfields", {force = true})

    -- System libraries
    if is_plat("linux") then
        add_syslinks("pthread")
    end
target_end()
