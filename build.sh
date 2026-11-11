#!/usr/bin/env bash
# SF-000 — one-line build: ./build.sh        (then: open "build/SF000_artefacts/Release/SF-000.app")
#   ./build.sh test    build, then run the offline audio and timing checks
#   ./build.sh perf    build, then measure the engine's and the screen's load on this machine
set -euo pipefail
cd "$(dirname "$0")"

JUCE_TAG="8.0.4"

if [ "$(uname)" = "Darwin" ] && ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode command line tools missing: run  xcode-select --install"; exit 1
fi
if ! command -v cmake >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    echo "Installing CMake via Homebrew..."; brew install cmake
  else
    echo "CMake missing and Homebrew not found. Install Homebrew (https://brew.sh) then re-run."; exit 1
  fi
fi
if [ ! -f JUCE/CMakeLists.txt ]; then
  echo "Cloning JUCE $JUCE_TAG..."
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git JUCE
fi

if [ "$(uname)" = "Darwin" ]; then
  cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
  JOBS="$(sysctl -n hw.ncpu)"
  APP="build/SF000_artefacts/Release/SF-000.app"
  BIN="$APP/Contents/MacOS/SF-000"
else
  cmake -B build -DCMAKE_BUILD_TYPE=Release
  JOBS="$(nproc)"
  APP="build/SF000_artefacts/Release/SF-000"
  BIN="$APP"
fi
cmake --build build --config Release -j "$JOBS"

echo
echo "Built: $APP"
if [ "${1:-}" = "test" ]; then
  "$BIN" --test
elif [ "${1:-}" = "perf" ]; then
  "$BIN" --perf
elif [ "$(uname)" = "Darwin" ]; then
  echo "Run:   open \"$APP\""
fi
