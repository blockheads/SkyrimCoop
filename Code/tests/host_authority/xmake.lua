-- The host's authority (HostAuthority, the Host* files in Code/sync) with no game: each rule against in-process
-- games on the host's network. Run: build/linux/x86_64/<mode>/HostAuthorityTests
target("HostAuthorityTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    -- Every host rule (Host*.cpp in a feature's folder) but those that bind the host's own game, which GameTests covers.
    add_files("*.cpp")
    add_files("../../sync/*/Host*.cpp|combat/HostAggro.cpp|combat/HostTakeBacks.cpp|combat/HostTraps.cpp" ..
              "|dialogue/HostConversation.cpp|game/HostService.cpp|game/HostServiceReload.cpp|game/HostWorldCalls.cpp")
    -- What those rules use, with no engine behind it.
    add_files("../../sync/combat/PartyThreat.cpp", "../../sync/core/JoinRefusal.cpp",
              "../../sync/physics/BodyAuthority.cpp", "../../sync/physics/BodyMotions.cpp",
              "../../sync/physics/BodyPlayback.cpp", "../../sync/physics/BodyPrediction.cpp",
              "../../sync/physics/BodyStream.cpp", "../../sync/core/RecipeBook.cpp",
              "../../sync/core/MessageTrail.cpp", "../../sync/core/Counters.cpp", "../../sync/tools/ReportPaths.cpp",
              "../../sync/dialogue/PartyOptionText.cpp", "../../sync/tools/DevCommands.cpp")
    add_includedirs(".", "../../sync")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv")
    add_syslinks("pthread")
target_end()
