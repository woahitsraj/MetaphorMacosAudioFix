#!/usr/bin/env bash
set -euo pipefail

cc="${CC:-$(command -v x86_64-w64-mingw32-gcc)}"
cxx="${CXX:-$(command -v x86_64-w64-mingw32-g++)}"

if [[ -z "${cc}" || -z "${cxx}" ]]; then
  echo "mingw-w64 cross compiler not found. Set CC/CXX or install x86_64-w64-mingw32-gcc and x86_64-w64-mingw32-g++." >&2
  exit 1
fi

cmake -B build/windows -S . \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER="${cc}" \
  -DCMAKE_CXX_COMPILER="${cxx}"

cmake --build build/windows --config Release
