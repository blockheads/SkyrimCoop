-- Every game's client logic with no engine (tier 1, docs/design/testing.md): the pure halves of the client's
-- hooks, listed one by one; a file that needs the engine does not build here. Run: build/linux/x86_64/<mode>/UnitTests
target("UnitTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    add_files("*.cpp|SaveTransferBench.cpp")
    -- Each layer's unit tests, beside what they test.
    add_files("../../sync/**_tests.cpp", "../../engine/**_tests.cpp", "../../plugin/**_tests.cpp", "../../harness/**_tests.cpp")
    add_files("../../sync/core/Counters.cpp", "../../sync/core/ActorIndex.cpp")
    add_files("../../sync/join/SaveTransfer.cpp", "../../engine/MainLoopTasks.cpp", "../../sync/core/TaskQueue.cpp", "../../sync/physics/PhysicsBodies.cpp",
              "../../sync/combat/PartyThreat.cpp", "../../sync/tools/DevCommands.cpp")
    add_files("../../sync/core/CharacterRecords.cpp", "../../sync/core/RecipeBook.cpp", "../../sync/core/CopyClock.cpp", "../../sync/core/CopyRecords.cpp", "../../sync/dialogue/PartyOptionText.cpp",
              "../../sync/core/MessageTrail.cpp", "../../plugin/Diagnostics/Perf.cpp", "../../sync/tools/ReportPaths.cpp", "../../plugin/Diagnostics/LogRing.cpp",
              "../../plugin/Diagnostics/Zip.cpp", "../../plugin/Diagnostics/Export.cpp", "../../plugin/Diagnostics/CrashReports.cpp")
    add_files("../../sync/scripts/PartyRules.cpp", "../../sync/scripts/ScriptTags.cpp",
              "../../sync/scripts/OwnEvents.cpp", "../../sync/scripts/StoryGifts.cpp", "../../sync/scripts/CodeReach.cpp",
              "../../engine/Misc/PartyArrivals.cpp", "../../engine/Misc/JudgedRules.cpp",
              "../../sync/scripts/StoryEventRules.cpp",
              "../../sync/physics/BodyPrediction.cpp", "../../sync/physics/BodyMotions.cpp", "../../sync/physics/BodyPlayback.cpp",
              "../../sync/physics/BodyAuthority.cpp", "../../sync/physics/BodyStream.cpp", "../../engine/HiddenCopies.cpp", "../../engine/GuestNpcs.cpp",
              "../../sync/animation/GraphStream.cpp", "../../sync/animation/NodeMemory.cpp",
              "../../sync/animation/GraphState.cpp")
    add_includedirs("..", "../../sync", "../../engine", "../../plugin", "../../harness")
    add_deps("SkyrimEncoding")
    add_packages("catch2", "zydis", "glm", "spdlog", "entt", "zlib", "snappy", "zstd", "lz4", "nlohmann_json")
    if is_plat("windows") then add_syslinks("bcrypt") end
target_end()

-- Compare actual save pairs offline, with the game's own image and encoding.
target("SaveTransferBench")
    set_kind("binary")
    set_optimize("fastest")
    set_group("Tools")
    set_languages("cxx23")
    add_files("SaveTransferBench.cpp", "../../sync/join/SaveTransfer.cpp")
    add_includedirs("../../sync")
    add_deps("SkyrimEncoding")
    add_packages("zstd", "lz4", "zlib")
    if is_plat("windows") then add_syslinks("bcrypt") end
target_end()
