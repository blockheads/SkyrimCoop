-- Every game's client logic with no engine (tier 1, docs/design/testing.md): the pure halves of the client's
-- hooks, listed one by one; a file that needs the engine does not build here. Run: build/linux/x86_64/<mode>/ClientTests
target("ClientTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    add_files("*.cpp")
    add_files("../../client/Services/CharacterRecords.cpp")
    add_files("../../client/Games/Skyrim/Papyrus/PartyRules.cpp", "../../client/Games/Skyrim/Papyrus/ScriptTags.cpp",
              "../../client/Games/Skyrim/Papyrus/OwnEvents.cpp", "../../client/Games/Skyrim/Papyrus/CodeReach.cpp",
              "../../client/Physics/BodyPrediction.cpp", "../../client/Physics/BodySends.cpp", "../../client/Physics/BodyPlayback.cpp",
              "../../client/Physics/BodyRelevance.cpp")
    add_includedirs("../../client", "../../client/Games/Skyrim")
    add_deps("SkyrimEncoding")
    add_packages("catch2", "zydis", "glm", "spdlog")
target_end()
