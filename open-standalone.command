#!/bin/zsh
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_PATH="$PROJECT_DIR/build/DrumSampler_artefacts/Debug/Standalone/ASTER Drum Rack.app"

if [[ ! -d "$APP_PATH" ]]; then
  echo "Standalone app was not found:"
  echo "$APP_PATH"
  echo
  echo "Run this first:"
  echo "  cmake --build build --config Debug"
  echo
  read -r "?Press Return to close..."
  exit 1
fi

echo "Opening ASTER Drum Rack Standalone..."
open -n "$APP_PATH"

