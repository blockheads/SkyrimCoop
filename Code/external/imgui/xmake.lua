-- ImGui static library build (manually vendored to avoid Wine MSVC package issues)
target("imgui")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("External")

    -- Core imgui files
    add_files(
        "imgui.cpp",
        "imgui_draw.cpp",
        "imgui_tables.cpp",
        "imgui_widgets.cpp",
        -- Include misc/cpp for std::string support
        "misc/cpp/imgui_stdlib.cpp"
    )

    -- Public include directory
    add_includedirs(".", {public = true})
    add_includedirs("misc/cpp", {public = true})

    if is_plat("windows") then
        add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    end
