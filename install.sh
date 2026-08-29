#!/usr/bin/env bash
# install.sh - build the release VST3 and install it to ~/.vst3
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

JOBS="${JOBS:-$(nproc)}"
VST3_SRC="build-release/valis_plugin_artefacts/Release/VST3/Valis.vst3"
VST3_DEST="${HOME}/.vst3/Valis.vst3"

echo
echo "==> Release plugin build"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DVALIS_BUILD_TESTS=OFF -DVALIS_BUILD_PLUGIN=ON
cmake --build build-release --parallel "$JOBS" --target valis_plugin_VST3

echo
echo "==> Installing to ${VST3_DEST}"
mkdir -p "${HOME}/.vst3"
rm -rf "${VST3_DEST}"
cp -r "${VST3_SRC}" "${VST3_DEST}"

echo "Done. Rescan plugins in your DAW to pick up Valis."
