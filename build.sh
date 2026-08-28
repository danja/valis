#!/usr/bin/env bash
# build.sh - the one-command build. Each stage echoes a heading before it runs.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-$(nproc)}"

echo
echo "==> Default build (core + plugin + tests)"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVALIS_BUILD_TESTS=ON
cmake --build build --parallel "$JOBS"

echo
echo "==> Unit tests"
ctest --test-dir build --output-on-failure

echo
echo "==> Release plugin (Standalone, VST3, LV2)"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DVALIS_BUILD_TESTS=OFF
cmake --build build-release --parallel "$JOBS"

if [[ "${VALIS_WITH_CLAP:-0}" == "1" ]]; then
  echo
  echo "==> CLAP build"
  cmake -S . -B build-clap -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DVALIS_BUILD_TESTS=OFF -DVALIS_WITH_CLAP=ON
  cmake --build build-clap --parallel "$JOBS"
fi

echo
echo "Build complete. Launch with ./valis"
