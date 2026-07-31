#!/usr/bin/env bash
set -euo pipefail

cc="${CC:-$(command -v x86_64-w64-mingw32-gcc)}"
cxx="${CXX:-$(command -v x86_64-w64-mingw32-g++)}"
host_cxx="${HOST_CXX:-$(command -v c++)}"

if [[ -z "${cc}" || -z "${cxx}" ]]; then
  echo "mingw-w64 cross compiler not found. Set CC/CXX or install x86_64-w64-mingw32-gcc and x86_64-w64-mingw32-g++." >&2
  exit 1
fi

test_dir="$(mktemp -d)"
trap 'rm -rf "${test_dir}"' EXIT
"${host_cxx}" -std=c++20 tests/spatial_stream_lifecycle.cpp -o "${test_dir}/spatial-stream-lifecycle"
"${test_dir}/spatial-stream-lifecycle"

cmake -B build/windows -S . \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER="${cc}" \
  -DCMAKE_CXX_COMPILER="${cxx}"

cmake --build build/windows --config Release
