-- The host's authority (HostAuthority, Code/client/Services/Host/) with no game: each rule against in-process
-- games on the host's network. Run: build/linux/x86_64/<mode>/BridgeTests
target("BridgeTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    add_files("*.cpp", "../../client/Services/Host/*.cpp")
    add_includedirs(".", "../../client")
    add_deps("SkyrimEncoding", "SkyrimCoopNetworking", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv")
    add_syslinks("pthread")
target_end()
