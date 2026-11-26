-- The host's authority (HostAuthority, the Host* files in Code/sync) with no game: each rule against in-process
-- games on the host's network. Run: build/linux/x86_64/<mode>/HostAuthorityTests
target("HostAuthorityTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    -- Conversation and aggro bind the host's game; GameTests covers their engine reads.
    add_files("*.cpp", 
              -- The host's parts (every Host*.cpp but the four above) and the physics rules.
              "../../sync/actorstate/HostValues.cpp", "../../sync/animation/HostGraph.cpp",
              "../../sync/characters/HostCharacters.cpp", "../../sync/characters/HostClaims.cpp",
              "../../sync/characters/HostRecords.cpp", "../../sync/combat/HostCombat.cpp",
              "../../sync/combat/PartyThreat.cpp", "../../sync/core/HostChat.cpp", "../../sync/core/HostNetwork.cpp",
              "../../sync/core/HostPlayers.cpp", "../../sync/core/HostRouter.cpp", "../../sync/core/JoinRefusal.cpp",
              "../../sync/crime/HostCrime.cpp", "../../sync/crime/HostEye.cpp", "../../sync/death/HostDeath.cpp",
              "../../sync/dialogue/HostDialogue.cpp", "../../sync/followers/HostFollowers.cpp",
              "../../sync/game/HostAuthority.cpp", "../../sync/items/HostItems.cpp",
              "../../sync/items/HostPartyItems.cpp", "../../sync/join/HostSaveTransfer.cpp",
              "../../sync/join/HostSession.cpp", "../../sync/magic/HostMagic.cpp",
              "../../sync/physics/HostBodies.cpp", "../../sync/scripts/HostPlayerNatives.cpp",
              "../../sync/scripts/HostStory.cpp", "../../sync/time/HostClock.cpp",
              "../../sync/time/HostTimeScale.cpp", "../../sync/time/HostWait.cpp", "../../sync/time/HostWeather.cpp",
              "../../sync/tools/HostDevTools.cpp", "../../sync/travel/HostLeash.cpp", "../../sync/travel/HostMap.cpp",
              "../../sync/world/HostObjects.cpp", "../../sync/world/HostWorldState.cpp",
              "../../sync/physics/BodyAuthority.cpp", "../../sync/physics/BodyMotions.cpp",
              "../../sync/physics/BodyPlayback.cpp", "../../sync/physics/BodyPrediction.cpp",
              "../../sync/physics/BodyStream.cpp",
              "../../sync/core/RecipeBook.cpp",
              "../../sync/core/MessageTrail.cpp", "../../sync/core/Counters.cpp", "../../sync/tools/ReportPaths.cpp",
              "../../sync/dialogue/PartyOptionText.cpp", "../../sync/tools/DevCommands.cpp")
    add_includedirs(".", "../../sync")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv")
    add_syslinks("pthread")
target_end()
