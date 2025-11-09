-- TiltedCore - Core utilities library
-- Merged into main repo for better control and debugging

target("TiltedCore")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("Libraries")

    add_files("*.cpp")
    add_includedirs(".", {public = true})
    add_headerfiles("*.hpp", "*.h")

    -- TiltedCore depends on mimalloc and hopscotch-map
    -- Mark as public so dependent targets inherit them
    add_packages("mimalloc", "hopscotch-map", {public = true})

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end
