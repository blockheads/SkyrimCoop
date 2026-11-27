-- Tier 2 (docs/design/testing.md): whole games in this process, each a World with its real services on a fake
-- engine (FakeEngine, Seams.cpp), and the host's rules. Run: build/linux/x86_64/<mode>/GameTests
target("GameTests")
    set_kind("binary")
    set_group("Tests")
    set_languages("cxx23")
    set_pcxxheader("Pch.h")
    -- The sync layer is its own target (Code/sync). Two engine files that are pure enough to run here.
    add_files("*.cpp", "../../engine/Interface/PartyMarkerLabel.cpp")
    add_deps("SkyrimCoopSync")
    -- ../../plugin: the diagnostics readouts the fake engine fills.
    add_includedirs(".", "../../plugin", "../../engine")
    -- The shared specs, which coop_test.py runs in Skyrim too.
    add_defines("COOP_SPECS_DIR=\"$(projectdir)/Code/tests/specs\"")
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("catch2", "glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "libuv", "nlohmann_json", "zstd", "lz4", "zlib")
    add_syslinks("pthread")
    if is_plat("windows") then add_syslinks("bcrypt") end
target_end()
