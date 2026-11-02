#!/usr/bin/env bash
set -euo pipefail

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake is missing. Install CMake 3.22+ (for example: brew install cmake)." >&2
  exit 1
fi
if ! command -v xcodebuild >/dev/null 2>&1; then
  echo "Full Xcode is missing. Install Xcode and select it with xcode-select." >&2
  exit 1
fi

cd "$(dirname "$0")"

# Compiles both Intel and Apple Silicon slices into a universal macOS VST3.
# Build requires network access on first run to download JUCE (unless JUCE_DIR
# is supplied as an environment variable with a local JUCE checkout).
if [[ -n "${JUCE_DIR:-}" ]]; then
  cmake -S . -B build -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 \
    -DJUCE_DIR="$JUCE_DIR"
else
  cmake -S . -B build -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
fi

cmake --build build --config Release --target VinylPilot_VST3
echo
echo "Build finished. Find Vinyl Pilot.vst3 in build/VinylPilot_artefacts/Release/VST3/"
echo "Copy that bundle into ~/Library/Audio/Plug-Ins/VST3/ and rescan VST3 in Live."
