-- The host's bridge (Code/client/Services/HostBridge.cpp) against a headless server: each relay
-- rule without Skyrim. Run: build/linux/x86_64/<mode>/BridgeTests
target("BridgeTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx20")
    set_pcxxheader("../../server/Pch.h")
    add_files("*.cpp", "../../client/Services/HostBridge.cpp")
    -- The server first: both have Components.h and World.h; the bridge needs only the server's.
    add_includedirs("../../server", "../../external", "../../external/cpp-httplib/include", "../../client")
    add_deps("SkyrimEncoding", "SkyrimTogetherServer")
    add_packages("catch2", "glm", "spdlog", "hopscotch-map", "rpmalloc", "enet6", "entt",
                 "nlohmann_json", "sqlite3", "lua", "sol2")
    add_syslinks("pthread")
target_end()
