target("CommonLib")
    set_kind("static")
    set_group("common")
    add_includedirs(".", "../", "../encoding", "../../build", {public = true})
    add_headerfiles("**.h")
    add_files("**.cpp")

    -- build/BuildInfo.h, on every xmake run (not only when configured): the join gate compares the
    -- build id (docs/design/join-flow.md, stage 1). Rewritten only when it changes, so an unchanged
    -- tree rebuilds nothing; BuildVersion.cpp is its only includer in the plugin.
    on_load(function (target)
        local function git(...)
            local args = {...}
            local output = try { function () return os.iorunv("git", args) end }
            return output and output:trim() or "unknown"
        end
        local content = "#pragma once\n// Written by Code/common/xmake.lua on every build.\n"
            .. format("#define BUILD_BRANCH \"%s\"\n", git("rev-parse", "--abbrev-ref", "HEAD"))
            .. format("#define BUILD_COMMIT \"%s\"\n", git("describe", "--tags", "--always", "--dirty"))
        local file = path.join(os.projectdir(), "build", "BuildInfo.h")
        if not os.isfile(file) or io.readfile(file) ~= content then
            io.writefile(file, content)
        end
    end)
