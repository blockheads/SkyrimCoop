#!/usr/bin/env bash
# clang-cl for xmake. xmake reads clang-cl's `/clang:-MF` dependency file without joining its
# "\"-continued lines (its gcc tool does join them), so every object appeared to depend on a file
# named "\" that never exists, and every build recompiled all ~430 files. Joining them here makes
# a one-file change a one-file rebuild.
#
# The precompiled header is compiled through a wrapper header xmake writes with
# `#pragma system_header`, so everything it includes counts as a system header and -MMD leaves it
# out: the PCH depended on nothing, and changing a header in it left a stale PCH that every
# object then refused. For that compile (-Yc) ask for every header (-MD).
real="$(dirname "$(readlink -f "$0")")/../LLVM/bin/clang-cl"
depfile=""
previous=""
creates_pch=0
for argument in "$@"; do
    [[ "$previous" == "/clang:-MF" ]] && depfile="${argument#/clang:}"
    [[ "$argument" == -Yc* || "$argument" == /Yc* ]] && creates_pch=1
    previous="$argument"
done
arguments=("$@")
if (( creates_pch )); then
    for i in "${!arguments[@]}"; do
        [[ "${arguments[$i]}" == "/clang:-MMD" ]] && arguments[$i]="/clang:-MD"
    done
fi
"$real" "${arguments[@]}"
status=$?
[[ -n "$depfile" && -f "$depfile" ]] && sed -i -z 's/\\\n//g' "$depfile"
exit $status
