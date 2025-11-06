
target("BaseLib")
    add_configfiles("BuildInfo.h.in")
    set_kind("static")
    set_group("common")
    add_includedirs(".", "../", "../../build", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp")

    -- Exclude Windows-specific UI files on non-Windows platforms
    if not is_plat("windows") then
        remove_files("dialogues/win/**.cpp")
    end

    add_deps("TiltedCore")
    add_packages(
        "sentry-native",
        "hopscotch-map",
        "gtest",
        "spdlog")
