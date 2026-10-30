#!/usr/bin/env bash
# Build and run the host unit tests for the crossover coefficient generator.
# Uses $CXX if set, otherwise c++ / g++ / clang++, otherwise "python -m ziglang c++"
# (pip install ziglang gives a portable clang on any platform).
set -euo pipefail

cd "$(dirname "$0")/.."

if [ -n "${CXX:-}" ]; then
  compiler=($CXX)
elif command -v c++ >/dev/null 2>&1; then
  compiler=(c++)
elif command -v g++ >/dev/null 2>&1; then
  compiler=(g++)
elif command -v clang++ >/dev/null 2>&1; then
  compiler=(clang++)
else
  # zig's bundled libc++ headers warn about nullability, unrelated to this code
  compiler=(python -m ziglang c++ -Wno-nullability-completeness)
fi

out_dir="${TMPDIR:-/tmp}/tas58xx_tests"
mkdir -p "$out_dir"

"${compiler[@]}" -std=c++17 -O1 -Wall -Wextra -Werror \
  tests/test_crossover.cpp \
  components/tas58xx/tas58xx_biquad.cpp \
  components/tas58xx/tas58xx_crossover.cpp \
  -o "$out_dir/test_crossover"

"$out_dir/test_crossover"
