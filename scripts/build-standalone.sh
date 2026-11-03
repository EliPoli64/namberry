#!/usr/bin/env bash
# Build the NAMBerry Standalone app.
#
# Usage:
#   ./scripts/build-standalone.sh            # configure + build (Release)
#   ./scripts/build-standalone.sh --clean    # wipe build dir first
#
# If the local sysroot created for machines without the JUCE Linux
# dev packages exists (/tmp/sysroot), it is wired up automatically.
# On a machine with the proper -devel packages installed, it is ignored.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-standalone"
TARGET="NAMBerry_Standalone"
CONFIG="Release"

if [[ "${1:-}" == "--clean" ]]; then
    echo "Removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

# --- Optional local sysroot (for systems without JUCE Linux -devel packages) ---
SYSR="/tmp/sysroot"
if [[ -d "${SYSR}/usr/include" ]]; then
    echo "Using local sysroot at ${SYSR}"
    export CPATH="${SYSR}/usr/include:${SYSR}/usr/include/freetype2${CPATH:+:${CPATH}}"
    export LIBRARY_PATH="${SYSR}/usr/lib64${LIBRARY_PATH:+:${LIBRARY_PATH}}"
    export LD_LIBRARY_PATH="${SYSR}/usr/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
    export PKG_CONFIG_SYSROOT_DIR="${SYSR}"
    export PKG_CONFIG_PATH="/tmp/pc:${SYSR}/usr/lib64/pkgconfig:${SYSR}/usr/share/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
fi

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${CONFIG}"
cmake --build "${BUILD_DIR}" --target "${TARGET}" -j"$(nproc)"

APP="${BUILD_DIR}/NAMBerry_artefacts/${CONFIG}/Standalone/NAMberry"
echo
echo "Standalone built: ${APP}"
