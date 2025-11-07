-- Merged libraries (formerly in Libraries/)
includes("TiltedCore")
includes("libraries")

-- Client and UI are Windows-only (also works for MinGW cross-compile)
if is_plat("windows") or is_plat("mingw") then
    includes("client")
    includes("immersive_elf")
    includes("immersive_launcher")
    includes("tp_process")
end

includes("common")
includes("components")
includes("base")
includes("admin_protocol")
includes("server")  -- Static library for embedded server
includes("encoding")

-- Skip unnecessary targets for faster builds
if not is_plat("mingw") then
    includes("tests")  -- Skip tests on MinGW (linking issues)
end

-- Uncomment these only if you need standalone server or admin tools:
-- includes("server_runner")  -- Standalone server executable (not needed for P2P)
-- includes("admin")  -- Admin CLI tool
