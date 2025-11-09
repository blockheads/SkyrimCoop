-- DirectXTK static library build (manually vendored to avoid Wine MSVC package issues)
-- Only includes the minimal set of files needed for UI overlay rendering

target("DirectXTK")
    -- Use object library for Wine MSVC to bypass broken lib.exe archiver
    if is_plat("windows") and get_config("sdk") == "/opt/msvc" then
        set_kind("object")
    else
        set_kind("static")
    end
    set_group("External")

    -- Compile shaders before building (SpriteBatch.cpp needs SpriteEffect_*.inc files)
    before_build(function (target)
        -- os.scriptdir() returns the directory containing this xmake.lua file
        -- which is Code/external/DirectXTK
        local shader_dir = path.join(os.scriptdir(), "Src/Shaders")
        local output_dir = path.join(shader_dir, "Compiled")

        -- Create output directory if it doesn't exist
        if not os.isdir(output_dir) then
            os.mkdir(output_dir)
        end

        -- Check if shaders already compiled
        local vs_out = path.join(output_dir, "SpriteEffect_SpriteVertexShader.inc")
        local ps_out = path.join(output_dir, "SpriteEffect_SpritePixelShader.inc")

        if os.isfile(vs_out) and os.isfile(ps_out) then
            -- Shaders already compiled
            return
        end

        print("Compiling DirectXTK shaders...")

        -- Find fxc.exe - in Wine MSVC it's in Windows Kits, on native Windows it's in PATH
        local fxc_path = nil
        local possible_paths = {
            "/opt/msvc/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe",
            "/opt/msvc/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe",
            "fxc.exe"  -- Try PATH for native Windows
        }

        for _, fxc_test in ipairs(possible_paths) do
            if os.isfile(fxc_test) or fxc_test == "fxc.exe" then
                fxc_path = fxc_test
                break
            end
        end

        if not fxc_path then
            raise("fxc.exe not found - required to compile DirectXTK shaders")
        end

        local shader_source = path.join(shader_dir, "SpriteEffect.fx")

        -- On Wine/Linux, we need to run fxc.exe through wine64 and convert paths to Windows format
        local use_wine = get_config("sdk") == "/opt/msvc"

        -- Helper function to convert Unix paths to Windows paths for Wine
        local function to_windows_path(unix_path)
            if not use_wine then
                return unix_path
            end
            -- Use winepath to convert Unix path to Windows path
            local result = os.iorunv("winepath", {"-w", unix_path})
            return result and result:trim() or unix_path
        end

        -- Compile vertex shader
        if not os.isfile(vs_out) then
            print("  -> SpriteEffect_SpriteVertexShader.inc")

            if use_wine then
                -- For Wine, convert paths to Windows format
                local win_shader = to_windows_path(shader_source)
                local win_out = to_windows_path(vs_out)
                local win_pdb = to_windows_path(path.join(output_dir, "SpriteEffect_SpriteVertexShader.pdb"))

                os.vrunv("wine64", {
                    fxc_path,
                    win_shader,
                    "/Tvs_4_0_level_9_1",
                    "/ESpriteVertexShader",
                    "/Fh" .. win_out,
                    "/Fd" .. win_pdb,
                    "/VnSpriteEffect_SpriteVertexShader",
                    "/nologo", "/WX", "/Ges", "/Zi", "/Zpc", "/Qstrip_reflect", "/Qstrip_debug"
                })
            else
                os.vrunv(fxc_path, {
                    shader_source,
                    "/Tvs_4_0_level_9_1",
                    "/ESpriteVertexShader",
                    "/Fh" .. vs_out,
                    "/Fd" .. path.join(output_dir, "SpriteEffect_SpriteVertexShader.pdb"),
                    "/VnSpriteEffect_SpriteVertexShader",
                    "/nologo", "/WX", "/Ges", "/Zi", "/Zpc", "/Qstrip_reflect", "/Qstrip_debug"
                })
            end
        end

        -- Compile pixel shader
        if not os.isfile(ps_out) then
            print("  -> SpriteEffect_SpritePixelShader.inc")

            if use_wine then
                -- For Wine, convert paths to Windows format
                local win_shader = to_windows_path(shader_source)
                local win_out = to_windows_path(ps_out)
                local win_pdb = to_windows_path(path.join(output_dir, "SpriteEffect_SpritePixelShader.pdb"))

                os.vrunv("wine64", {
                    fxc_path,
                    win_shader,
                    "/Tps_4_0_level_9_1",
                    "/ESpritePixelShader",
                    "/Fh" .. win_out,
                    "/Fd" .. win_pdb,
                    "/VnSpriteEffect_SpritePixelShader",
                    "/nologo", "/WX", "/Ges", "/Zi", "/Zpc", "/Qstrip_reflect", "/Qstrip_debug"
                })
            else
                os.vrunv(fxc_path, {
                    shader_source,
                    "/Tps_4_0_level_9_1",
                    "/ESpritePixelShader",
                    "/Fh" .. ps_out,
                    "/Fd" .. path.join(output_dir, "SpriteEffect_SpritePixelShader.pdb"),
                    "/VnSpriteEffect_SpritePixelShader",
                    "/nologo", "/WX", "/Ges", "/Zi", "/Zpc", "/Qstrip_reflect", "/Qstrip_debug"
                })
            end
        end
    end)

    -- Only compile what's actually needed for SpriteBatch + texture loading
    -- Used by Code/libraries/ui/OverlayRenderHandlerD3D11.cpp
    add_files(
        "Src/SpriteBatch.cpp",
        "Src/CommonStates.cpp",
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

    -- Add shader compiled output directory to include paths
    add_includedirs("Src/Shaders/Compiled")

    -- Link against required system libraries
    add_syslinks("d3d11", "dxguid", "uuid", "ole32")

    if is_plat("windows") then
        add_defines("_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    end
