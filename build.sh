#!/usr/bin/env bash
# Build both NAMBerry targets: Standalone and VST3.
#
# Usage:
#   ./build.sh            # configure + build both (Release)
#   ./build.sh --clean    # wipe both build directories first

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"${ROOT}/scripts/build-standalone.sh" "$@"
"${ROOT}/scripts/build-vst3.sh" "$@"

echo
echo "Built both targets:"
echo "  Standalone: ${ROOT}/build-standalone/NAMBerry_artefacts/Release/Standalone/NAMberry"
echo "  VST3:       ${ROOT}/build-vst3/NAMBerry_artefacts/Release/VST3/NAMberry.vst3"
