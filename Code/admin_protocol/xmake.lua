
target("AdminProtocol")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("common")
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end
    add_includedirs(".", "../", {public = true})
    add_headerfiles("**.h", {prefixdir = "AdminProtocol"})
    add_files("**.cpp")
    add_deps("TiltedCore")
    add_packages("hopscotch-map")
