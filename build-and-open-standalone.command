#!/bin/zsh
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_PATH="$PROJECT_DIR/build/DrumSampler_artefacts/Debug/Standalone/ASTER Drum Rack.app"

cd "$PROJECT_DIR"

if [[ -f "$PROJECT_DIR/ui-prototype/package.json" ]]; then
  echo "Building Web UI..."
  npm --prefix "$PROJECT_DIR/ui-prototype" run build
fi

if [[ ! -d "$PROJECT_DIR/build" ]]; then
  echo "Configuring CMake build directory..."
  cmake -S "$PROJECT_DIR" -B "$PROJECT_DIR/build" -DCMAKE_BUILD_TYPE=Debug
fi

echo "Building Standalone app..."
cmake --build "$PROJECT_DIR/build" --config Debug --target DrumSampler_Standalone

if [[ ! -d "$APP_PATH" ]]; then
  echo "Standalone app was not created:"
  echo "$APP_PATH"
  read -r "?Press Return to close..."
  exit 1
fi

echo "Opening ASTER Drum Rack Standalone..."
open -n "$APP_PATH"

