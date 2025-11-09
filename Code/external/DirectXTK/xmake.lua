-- DirectXTK static library build (manually vendored to avoid Wine MSVC package issues)
-- Only includes the minimal set of files needed for UI overlay rendering
target("DirectXTK")
    set_kind("static")
    set_group("External")

    -- Only compile what's actually needed for SpriteBatch + texture loading
    -- Used by Code/libraries/ui/OverlayRenderHandlerD3D11.cpp
    -- Note: SpriteBatch requires compiled shaders which need fxc.exe (not available in Wine MSVC)
    if get_config("sdk") ~= "/opt/msvc" then
        add_files(
            "Src/SpriteBatch.cpp",
            "Src/CommonStates.cpp"
        )
    end

    add_files(
        "Src/SimpleMath.cpp",
        "Src/DDSTextureLoader.cpp",
        "Src/WICTextureLoader.cpp",
        -- Dependencies of the above
        "Src/BufferHelpers.cpp",
        "Src/DirectXHelpers.cpp",
        "Src/GraphicsMemory.cpp",
        "Src/VertexTypes.cpp",
        "Src/pch.cpp"
    )

    -- Public include directories
    add_includedirs("Inc", {public = true})

    -- Private include for implementation
    add_includedirs("Src", "Audio")

    -- Link against required system libraries
    add_syslinks("d3d11", "dxguid", "uuid", "ole32")

    if is_plat("windows") then
        add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    end
