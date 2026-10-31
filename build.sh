#!/usr/bin/env bash
# build.sh - the one-command build. Each stage echoes a heading before it runs.
# By default it builds the Release plugin and tools; set VALIS_TESTS to build and run the Debug tests as well.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-$(nproc)}"

# The neural ears of valis-shot (listen, sounds_like) need ONNX Runtime, which the first configure downloads, pinned and
# checksummed. The model weights are not part of the build: scripts/shot/fetch-models.sh. VALIS_WITH_ONNX=0 skips it
# (offline, or no wish for the dependency); CMake's own default stays OFF.
ONNX="${VALIS_WITH_ONNX:-1}"
case "$ONNX" in
  0) ONNX_FLAG=OFF ;;
  1) ONNX_FLAG=ON ;;
  *) echo "VALIS_WITH_ONNX must be 0 or 1 (got '$ONNX')" >&2; exit 2 ;;
esac

# Tests are opt-in: the full Debug suite takes about five minutes, most of it one test (label "slow").
#   VALIS_TESTS=1 ./build.sh       Debug build with tests, all tests, then the Release build
#   VALIS_TESTS=quick ./build.sh   the same, without the tests labelled "slow"
case "${VALIS_TESTS:-0}" in
  0) ;;
  1|quick)
    echo
    echo "==> Default build (core + plugin + tests)"
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DVALIS_BUILD_TESTS=ON \
          -DVALIS_WITH_ONNX=$ONNX_FLAG
    cmake --build build --parallel "$JOBS"

    echo
    echo "==> Unit tests"
    if [[ "$VALIS_TESTS" == "quick" ]]; then
      ctest --test-dir build --output-on-failure --parallel "$JOBS" -LE slow
    else
      ctest --test-dir build --output-on-failure --parallel "$JOBS"
    fi
    ;;
  *) echo "VALIS_TESTS must be 0, 1 or quick (got '$VALIS_TESTS')" >&2; exit 2 ;;
esac

echo
echo "==> Release plugin (Standalone, VST3, LV2)"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DVALIS_BUILD_TESTS=OFF \
      -DVALIS_WITH_ONNX=$ONNX_FLAG
cmake --build build-release --parallel "$JOBS"

VALIDATOR="${VALIDATOR:-$HOME/VST_SDK/build/bin/Release/validator}"
if [[ -x "$VALIDATOR" ]]; then
  echo
  echo "==> VST3 conformance check"
  "$VALIDATOR" build-release/valis_plugin_artefacts/Release/VST3/Valis.vst3
else
  echo
  if [[ -L "$VALIDATOR" && ! -e "$VALIDATOR" ]]; then
    why="a symlink that is broken (is the drive mounted?)"
  elif [[ -e "$VALIDATOR" ]]; then
    why="the file exists but is not executable"
  elif [[ ! -d "$(dirname "$VALIDATOR")" ]]; then
    why="directory $(dirname "$VALIDATOR") does not exist: the SDK is not built there"
  else
    why="no such file"
  fi
  echo "==> VST3 conformance check (skipped: tried $VALIDATOR, $why)"
  echo "    Build the SDK's validator and set VALIDATOR=/path/to/validator to enable it."
fi

if [[ "${VALIS_WITH_CLAP:-0}" == "1" ]]; then
  echo
  echo "==> CLAP build"
  cmake -S . -B build-clap -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DVALIS_BUILD_TESTS=OFF -DVALIS_WITH_CLAP=ON
  cmake --build build-clap --parallel "$JOBS"
fi

echo
echo "Build complete. Launch with ./valis"
