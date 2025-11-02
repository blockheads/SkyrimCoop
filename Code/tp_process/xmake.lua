
target("TPProcess")
    set_kind("binary")
    set_group("Client")

    -- CEF is built with static runtime (/MT), so TPProcess must match
    -- Force MT runtime at both compile and link stages
    if is_mode("releasedbg") or is_mode("release") then
        set_runtimes("MT")
        add_cxflags("/MT", {tools = {"cl"}, force = true})
        -- Remove all MD libraries and force MT
        add_ldflags("/NODEFAULTLIB:msvcprt.lib", "/NODEFAULTLIB:msvcrt.lib", {force = true})
        add_ldflags("libcpmt.lib", "libcmt.lib", {force = true})
    end

    if is_plat("windows") then
        add_ldflags("/subsystem:windows")
    end

    add_includedirs(
        ".")
    add_headerfiles("*.h")
    add_files(
        "*.cpp",
        "process.rc")
    add_deps("SkyrimCoopUIProcess", "TiltedCore")
    add_packages("cef", "hopscotch-map")

    -- Override any inherited runtime settings from dependencies
    after_load(function (target)
        target:set("runtimes", "MT")
    end)