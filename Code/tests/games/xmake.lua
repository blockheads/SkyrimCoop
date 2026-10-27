-- Tier 2 (docs/design/testing.md): whole games in this process, each a World with its real services on a fake
-- engine (FakeEngine, Seams.cpp), and the host's rules. Run: build/linux/x86_64/<mode>/GameTests
target("GameTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    add_files("*.cpp")
    add_files("../../client/Services/Host/*.cpp", "../../client/Physics/*.cpp", "../../client/Services/HostServiceReload.cpp")
    add_files("../../client/Services/Generic/TransportService.cpp", "../../client/Services/Generic/CutsceneService.cpp",
              "../../client/Services/Generic/RunnerService.cpp", "../../client/TaskQueue.cpp",
              "../../client/ScriptValues.cpp", "../../client/SessionIds.cpp",
              "../../client/Services/Generic/CharacterService.cpp", "../../client/Services/Generic/TravelService.cpp",
              "../../client/Services/Generic/SessionService.cpp", "../../client/Services/Generic/ActorValueService.cpp",
              "../../client/Services/Generic/PlayerService.cpp", "../../client/Services/Generic/DeathService.cpp",
              "../../client/Services/Generic/InventoryService.cpp",
              "../../client/Systems/InterpolationSystem.cpp",
              "../../client/Services/Guest/GuestWorldState.cpp", "../../client/Services/Guest/GuestClock.cpp",
              "../../client/Services/Guest/GuestWeather.cpp", "../../client/Services/Generic/BodyService.cpp",
              "../../client/Services/Guest/GuestHands.cpp", "../../client/Services/Guest/GuestWait.cpp",
              "../../client/Services/Generic/MapService.cpp",
              "../../client/Services/Generic/TimeScaleService.cpp", "../../client/Services/Generic/WaitService.cpp", "../../client/Services/Generic/CrimeService.cpp", "../../client/Services/Generic/MagicService.cpp",
              "../../client/Services/Generic/EffectVisualsService.cpp", "../../client/Services/Generic/AiStateService.cpp",
              "../../client/Services/Generic/CombatService.cpp",
              "../../client/Services/Generic/ObjectService.cpp",
              "../../client/Services/Generic/DialogueService.cpp", "../../client/Services/Guest/GuestDialogue.cpp",
              "../../client/Services/Generic/SteamService.cpp", "../../client/Services/Generic/SaveService.cpp",
              "../../client/Services/CharacterRecords.cpp",
              "../../client/Services/SaveTransfer.cpp", "../../client/Services/Generic/JoinService.cpp",
              "../../client/Services/Guest/GuestRecord.cpp")
    add_files("../../client/Games/Skyrim/Papyrus/PartyRules.cpp")
    add_includedirs(".", "../../client", "../../client/Games/Skyrim")
    -- The shared specs, which coop_test.py runs in Skyrim too.
    add_defines("COOP_SPECS_DIR=\"$(projectdir)/Code/tests/specs\"")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv", "nlohmann_json")
    add_syslinks("pthread")
target_end()
