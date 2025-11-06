-- TiltedCore - Core utilities library
-- Merged into main repo for better control and debugging

target("TiltedCore")
    set_kind("static")
    set_group("Libraries")

    add_files("*.cpp")
    add_includedirs(".", {public = true})
    add_headerfiles("*.hpp", "*.h")

    -- TiltedCore depends on rpmalloc and hopscotch-map
    -- Mark as public so dependent targets inherit them
    add_packages("rpmalloc", "hopscotch-map", {public = true})

    -- Ensure debug symbols
    if is_plat("windows") then
        set_symbols("debug")
    end
