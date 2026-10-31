-- Every game's client logic with no engine (tier 1, docs/design/testing.md): the pure halves of the client's
-- hooks, listed one by one; a file that needs the engine does not build here. Run: build/linux/x86_64/<mode>/ClientTests
target("ClientTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    add_files("*.cpp")
    add_files("../../client/MainLoopTasks.cpp", "../../client/TaskQueue.cpp")
    add_files("../../client/Services/CharacterRecords.cpp", "../../client/RecipeBook.cpp", "../../client/Services/CopyClock.cpp", "../../client/Services/Generic/PartyOptionText.cpp",
              "../../client/Diagnostics/MessageTrail.cpp", "../../client/Diagnostics/ReportPaths.cpp", "../../client/Diagnostics/LogRing.cpp",
              "../../client/Diagnostics/Zip.cpp", "../../client/Diagnostics/Export.cpp", "../../client/Diagnostics/CrashReports.cpp")
    add_files("../../client/Games/Skyrim/Papyrus/PartyRules.cpp", "../../client/Games/Skyrim/Papyrus/ScriptTags.cpp",
              "../../client/Games/Skyrim/Papyrus/OwnEvents.cpp", "../../client/Games/Skyrim/Papyrus/StoryGifts.cpp", "../../client/Games/Skyrim/Papyrus/CodeReach.cpp",
              "../../client/Games/Skyrim/Misc/PartyArrivals.cpp", "../../client/Games/Skyrim/Misc/JudgedRules.cpp",
              "../../client/Games/Skyrim/Misc/StoryEventRules.cpp",
              "../../client/Physics/BodyPrediction.cpp", "../../client/Physics/BodySends.cpp", "../../client/Physics/BodyPlayback.cpp",
              "../../client/Physics/BodyRelevance.cpp")
    add_includedirs("../../client", "../../client/Games/Skyrim")
    add_deps("SkyrimEncoding")
    add_packages("catch2", "zydis", "glm", "spdlog", "zlib")
target_end()
