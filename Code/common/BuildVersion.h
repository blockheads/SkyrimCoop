#pragma once

// The build's git branch and commit. Only BuildVersion.cpp includes the generated BuildInfo.h, so
// a new commit recompiles that one file instead of everything behind a precompiled header.
const char* BuildBranch() noexcept;
const char* BuildCommit() noexcept;
