
target("BaseLib")
    add_configfiles("BuildInfo.h.in")
    set_kind("static")
    set_group("common")
    add_includedirs(".", "../", "../../build", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp")
    -- Exclude Windows-specific dialog files on non-Windows platforms
    if not is_plat("windows") and not is_plat("mingw") then
        remove_files("dialogues/win/**.cpp")
    end
    add_deps("TiltedCore")
    add_packages("hopscotch-map", "gtest", "spdlog")
