#!/usr/bin/env bash
# Build the NAMBerry VST3 bundle.
#
# Usage:
#   ./scripts/build-vst3.sh            # configure + build (Release)
#   ./scripts/build-vst3.sh --clean    # wipe build dir first
#
# Wires up the optional /tmp/sysroot if present (systems without JUCE
# Linux -devel packages). Ignored on fully provisioned machines.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build-vst3"
TARGET="NAMBerry_VST3"
CONFIG="Release"

if [[ "${1:-}" == "--clean" ]]; then
    echo "Removing ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

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

BUNDLE="${BUILD_DIR}/NAMBerry_artefacts/${CONFIG}/VST3/NAMberry.vst3"
echo
echo "VST3 built: ${BUNDLE}"
