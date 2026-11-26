-- The sync layer (docs/architecture.md): every decision the mod makes, with no Skyrim. Every .cpp under Code/sync,
-- built once for the plugin and for GameTests. scripts/check_layout.py --strict checks what it includes.
target("SkyrimCoopSync")
    -- Objects, like the plugin: every file is linked, whoever refers to it.
    set_kind("object")
    set_group("Client")
    set_languages("cxx23")
    set_pcxxheader("SyncPch.h")
    add_files("**.cpp")
    add_includedirs(".", {public = true})
    add_includedirs("../external")
    add_defines(has_config("test_harness") and "COOP_TEST_HARNESS=1" or "COOP_TEST_HARNESS=0",
                has_config("profiler") and "COOP_PROFILER=1" or "COOP_PROFILER=0")
    if is_plat("windows") then
        -- As the plugin's own files see spdlog (ClientPch.h).
        add_defines("SPDLOG_WCHAR_FILENAMES", "SPDLOG_WCHAR_TO_UTF8_SUPPORT")
    end
    if is_plat("windows") and has_config("profiler") then
        add_packages("tracy")
        add_defines("TRACY_ENABLE")
    end
    add_deps("SkyrimEncoding", "Network", "CommonLib")
    add_packages("glm", "spdlog", "rpmalloc", "enet6", "entt", "snappy", "nlohmann_json", "zstd", "lz4", "zlib", "zydis")
target_end()
