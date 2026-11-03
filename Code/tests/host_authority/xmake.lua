-- The host's authority (HostAuthority, Code/client/Services/Host/) with no game: each rule against in-process
-- games on the host's network. Run: build/linux/x86_64/<mode>/HostAuthorityTests
target("HostAuthorityTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    -- Conversation and aggro bind the host's game; GameTests covers their engine reads.
    add_files("*.cpp", "../../client/Services/Host/*.cpp|HostConversation.cpp|HostAggro.cpp", "../../client/Physics/*.cpp", "../../client/RecipeBook.cpp",
              "../../client/Diagnostics/MessageTrail.cpp", "../../client/Diagnostics/ReportPaths.cpp",
              "../../client/Services/Generic/PartyOptionText.cpp")
    add_includedirs(".", "../../client")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv")
    add_syslinks("pthread")
target_end()
