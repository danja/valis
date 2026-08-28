#!/usr/bin/env bash
# scripts/generate-docs.sh
#
# Regenerates the parts of the manual that are derived from the ontology, so
# they cannot drift from what the plugin actually offers. Run it after editing
# vocabs/valis.ttl.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

RENDER="build/valis_render_artefacts/Debug/valis_render"
if [[ ! -x "$RENDER" ]]; then
  RENDER="build-release/valis_render_artefacts/Release/valis_render"
fi
if [[ ! -x "$RENDER" ]]; then
  echo "valis-render is not built. Run ./build.sh first." >&2
  exit 1
fi

echo "Generating docs/manual/elements.md from vocabs/valis.ttl"
"$RENDER" --elements 2>/dev/null > docs/manual/elements.md

echo "Done. $(grep -c '^## ' docs/manual/elements.md) elements documented."
