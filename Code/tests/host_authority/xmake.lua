-- The host's authority (HostAuthority, Code/client/Services/Host/) with no game: each rule against in-process
-- games on the host's network. Run: build/linux/x86_64/<mode>/HostAuthorityTests
target("HostAuthorityTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    -- HostConversation is the host's game in a conversation, not a rule: it needs the game.
    add_files("*.cpp", "../../client/Services/Host/*.cpp|HostConversation.cpp", "../../client/Physics/*.cpp", "../../client/RecipeBook.cpp")
    add_includedirs(".", "../../client")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv")
    add_syslinks("pthread")
target_end()
