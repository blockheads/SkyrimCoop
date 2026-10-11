#!/bin/sh
# CMake's Ninja generator runs MSVC resource compiles through cmcldeps:
#   cmcldeps RC <source> <depfile> <output> <prefix> <cl> <rc> <rc arguments...>
# cmcldeps only exists on Windows, so on Linux CMake leaves the bare "RC" as the command (this
# file, installed as msvc-bin/RC). Resources include nothing worth tracking: write an empty
# dependency file and run the resource compiler.
depfile="$2"
rc="$6"
shift 6
: > "$depfile"
exec "$rc" "$@"
