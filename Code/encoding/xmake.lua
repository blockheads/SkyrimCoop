local function build_encoding(name)
target(name)
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("common")
    add_includedirs(".", "../", {public = true})
    add_headerfiles("**.h|Structs/Skyrim/**", {prefixdir = "Encoding"})
    add_files("**.cpp|Structs/Skyrim/**")
    set_pcxxheader("EncodingPch.h")

    if is_plat("linux") then
        add_cxxflags("-fPIC")
    end    

    add_files("Structs/Skyrim/**.cpp")
    add_headerfiles("Structs/Skyrim/**.h")
    add_includedirs("Structs/Skyrim")

    add_deps("TiltedCore")
    add_packages("hopscotch-map", "glm")
end

build_encoding("SkyrimEncoding")
