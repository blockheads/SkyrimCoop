-- Skip tests on MinGW cross-compilation (tests run natively on Linux)
-- Tests can still be built on native Windows or Linux
target("TPTests")
    set_kind("binary")
    set_group("Tests")

    add_includedirs(".", "../encoding", "../native_client",
                    "../native_client/third_party",
                    "../server", "../common", "../components", "../base",
                    "../admin_protocol", "../libraries/networking", "../external",
                    "../native_client/game_bridge", "../native_client/transport")
    add_headerfiles("**.h")
    add_files("*.cpp")
    add_files("../native_client/game_bridge/pointer_table.cpp")
    add_files("../native_client/game_bridge/tcp_client.cpp")
    add_files("../native_client/transport/*.cpp")
    if is_plat("linux") then
        add_files("../native_client/game_bridge/proc_memory.cpp")
    end
    add_deps("SkyrimEncoding", "SkyrimTogetherServer")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map", "enet6", "entt",
                 "sqlite3", "lua", "sol2")

includes("integration")
includes("replay")
