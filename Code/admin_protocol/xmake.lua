
target("AdminProtocol")
    devbuild_shared()
    set_group("common")
    if is_plat("linux") then
        add_cxflags("-fPIC")
    end
    add_includedirs(".", "../", {public = true})
    add_headerfiles("**.h", {prefixdir = "AdminProtocol"})
    add_files("**.cpp")
    add_deps("TiltedCore")
    add_packages("hopscotch-map")
