
target("BaseLib")
    add_configfiles("BuildInfo.h.in")
    devbuild_shared()
    set_group("common")
    add_includedirs(".", "../", "../../build", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp|tests/**.cpp")
    -- Exclude Windows-specific dialog files on non-Windows platforms
    if not is_plat("windows") and not is_plat("mingw") then
        remove_files("dialogues/win/**.cpp")
    end
    add_deps("TiltedCore")
    add_packages("hopscotch-map", "spdlog")
    -- comctl32 needed for TaskDialogIndirect (dialogues/win/TaskDialog.cpp)
    if is_plat("windows") or is_plat("mingw") then
        add_syslinks("comctl32")
    end
