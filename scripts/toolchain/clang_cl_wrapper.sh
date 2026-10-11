#!/usr/bin/env bash
# clang-cl for xmake. xmake reads clang-cl's `/clang:-MF` dependency file without joining its
# "\"-continued lines (its gcc tool does join them), so every object appeared to depend on a file
# named "\" that never exists, and every build recompiled all ~430 files. Joining them here makes
# a one-file change a one-file rebuild.
real="$(dirname "$(readlink -f "$0")")/../LLVM/bin/clang-cl"
depfile=""
previous=""
for argument in "$@"; do
    [[ "$previous" == "/clang:-MF" ]] && depfile="${argument#/clang:}"
    previous="$argument"
done
"$real" "$@"
status=$?
[[ -n "$depfile" && -f "$depfile" ]] && sed -i -z 's/\\\n//g' "$depfile"
exit $status
