#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="$(sed -nE 's/^project\(AsterDrumLab VERSION ([0-9.]+)\)$/\1/p' "$ROOT_DIR/CMakeLists.txt")"
[[ -n "$VERSION" ]] || { echo "Could not read the project version from CMakeLists.txt" >&2; exit 1; }
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release-macos}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
WORK_DIR="$BUILD_DIR/package"
PRODUCT_NAME="ASTER Drum Lab"
OUTPUT_PKG="$DIST_DIR/ASTER-Drum-Lab-${VERSION}-macOS.pkg"
AAX_SDK_PATH="${AAX_SDK_PATH:-$HOME/SDKs/aax-sdk-2-9-0}"
AAX_ENABLED=0
AAX_CMAKE_PATH=""
if [[ -d "$AAX_SDK_PATH/Interfaces/ACF" ]]; then
  AAX_ENABLED=1
  AAX_CMAKE_PATH="$AAX_SDK_PATH"
fi
UNSIGNED=0
SKIP_BUILD=0
SKIP_NOTARIZE=0

usage() {
  cat <<'EOF'
Usage: packaging/macos/build-pkg.sh [options]

Options:
  --unsigned        Build a local test package without Developer ID signing.
  --skip-build      Reuse the existing Release build.
  --skip-notarize   Sign the package but skip Apple notarization.

Signed release environment:
  APP_SIGN_IDENTITY       Developer ID Application: ...
  INSTALLER_SIGN_IDENTITY Developer ID Installer: ...
  NOTARY_PROFILE          Keychain profile created by `xcrun notarytool store-credentials`.
EOF
}

while (($#)); do
  case "$1" in
    --unsigned) UNSIGNED=1 ;;
    --skip-build) SKIP_BUILD=1 ;;
    --skip-notarize) SKIP_NOTARIZE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [[ "$UNSIGNED" -eq 0 ]]; then
  : "${APP_SIGN_IDENTITY:?Set APP_SIGN_IDENTITY to a Developer ID Application identity}"
  : "${INSTALLER_SIGN_IDENTITY:?Set INSTALLER_SIGN_IDENTITY to a Developer ID Installer identity}"
  if [[ "$SKIP_NOTARIZE" -eq 0 ]]; then
    : "${NOTARY_PROFILE:?Set NOTARY_PROFILE to a notarytool keychain profile}"
  fi
fi

if [[ "$SKIP_BUILD" -eq 0 ]]; then
  npm --prefix "$ROOT_DIR/ui-prototype" ci
  npm --prefix "$ROOT_DIR/ui-prototype" run build
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
    -DASTER_COPY_PLUGIN_AFTER_BUILD=OFF \
    "-DASTER_AAX_SDK_PATH=$AAX_CMAKE_PATH"
  cmake --build "$BUILD_DIR" --config Release --parallel
fi

ARTEFACTS="$BUILD_DIR/DrumSampler_artefacts/Release"
AU="$ARTEFACTS/AU/$PRODUCT_NAME.component"
VST3="$ARTEFACTS/VST3/$PRODUCT_NAME.vst3"
AAX="$ARTEFACTS/AAX/$PRODUCT_NAME.aaxplugin"

for path in "$AU" "$VST3"; do
  [[ -e "$path" ]] || { echo "Missing build artefact: $path" >&2; exit 1; }
done
if [[ "$AAX_ENABLED" -eq 1 ]]; then
  [[ -e "$AAX" ]] || { echo "Missing build artefact: $AAX" >&2; exit 1; }
fi

rm -rf "$WORK_DIR"
mkdir -p \
  "$WORK_DIR/root-au" \
  "$WORK_DIR/root-vst3" \
  "$WORK_DIR/root-aax" \
  "$WORK_DIR/packages" \
  "$DIST_DIR"

ditto "$AU" "$WORK_DIR/root-au/$PRODUCT_NAME.component"
ditto "$VST3" "$WORK_DIR/root-vst3/$PRODUCT_NAME.vst3"
if [[ "$AAX_ENABLED" -eq 1 ]]; then
  ditto "$AAX" "$WORK_DIR/root-aax/$PRODUCT_NAME.aaxplugin"
fi

STAGED_AU="$WORK_DIR/root-au/$PRODUCT_NAME.component"
STAGED_VST3="$WORK_DIR/root-vst3/$PRODUCT_NAME.vst3"
STAGED_AAX="$WORK_DIR/root-aax/$PRODUCT_NAME.aaxplugin"

BUNDLES=("$STAGED_AU" "$STAGED_VST3")
[[ "$AAX_ENABLED" -eq 1 ]] && BUNDLES+=("$STAGED_AAX")

if [[ "$UNSIGNED" -eq 0 ]]; then
  for bundle in "${BUNDLES[@]}"; do
    codesign --force --deep --strict --options runtime --timestamp \
      --sign "$APP_SIGN_IDENTITY" "$bundle"
    codesign --verify --deep --strict --verbose=2 "$bundle"
  done
else
  for bundle in "${BUNDLES[@]}"; do
    codesign --force --deep --sign - "$bundle"
    codesign --verify --deep --strict --verbose=2 "$bundle"
  done
fi

AU_PKG="$WORK_DIR/packages/com.enigma.asterdrumlab.au.pkg"
VST3_PKG="$WORK_DIR/packages/com.enigma.asterdrumlab.vst3.pkg"
AAX_PKG="$WORK_DIR/packages/com.enigma.asterdrumlab.aax.pkg"
AU_COMPONENTS="$WORK_DIR/packages/au-components.plist"
VST3_COMPONENTS="$WORK_DIR/packages/vst3-components.plist"
AAX_COMPONENTS="$WORK_DIR/packages/aax-components.plist"

rm -f "$AU_PKG" "$VST3_PKG" "$AAX_PKG" "$AU_COMPONENTS" "$VST3_COMPONENTS" "$AAX_COMPONENTS"
pkgbuild --analyze --root "$WORK_DIR/root-au" "$AU_COMPONENTS"
pkgbuild --analyze --root "$WORK_DIR/root-vst3" "$VST3_COMPONENTS"
if [[ "$AAX_ENABLED" -eq 1 ]]; then
  pkgbuild --analyze --root "$WORK_DIR/root-aax" "$AAX_COMPONENTS"
fi

pkgbuild \
  --root "$WORK_DIR/root-au" \
  --component-plist "$AU_COMPONENTS" \
  --identifier "com.enigma.asterdrumlab.au.pkg" \
  --version "$VERSION" \
  --install-location "/Library/Audio/Plug-Ins/Components" \
  "$AU_PKG"

pkgbuild \
  --root "$WORK_DIR/root-vst3" \
  --component-plist "$VST3_COMPONENTS" \
  --identifier "com.enigma.asterdrumlab.vst3.pkg" \
  --version "$VERSION" \
  --install-location "/Library/Audio/Plug-Ins/VST3" \
  "$VST3_PKG"

if [[ "$AAX_ENABLED" -eq 1 ]]; then
  pkgbuild \
    --root "$WORK_DIR/root-aax" \
    --component-plist "$AAX_COMPONENTS" \
    --identifier "com.enigma.asterdrumlab.aax.pkg" \
    --version "$VERSION" \
    --install-location "/Library/Application Support/Avid/Audio/Plug-Ins" \
    "$AAX_PKG"
fi

PRODUCT_PACKAGES=(--package "$AU_PKG" --package "$VST3_PKG")
[[ "$AAX_ENABLED" -eq 1 ]] && PRODUCT_PACKAGES+=(--package "$AAX_PKG")

rm -f "$OUTPUT_PKG"
if [[ "$UNSIGNED" -eq 1 ]]; then
  productbuild \
    "${PRODUCT_PACKAGES[@]}" \
    "$OUTPUT_PKG"
else
  productbuild \
    "${PRODUCT_PACKAGES[@]}" \
    --sign "$INSTALLER_SIGN_IDENTITY" \
    --timestamp \
    "$OUTPUT_PKG"
fi

pkgutil --check-signature "$OUTPUT_PKG" || [[ "$UNSIGNED" -eq 1 ]]

if [[ "$UNSIGNED" -eq 0 && "$SKIP_NOTARIZE" -eq 0 ]]; then
  xcrun notarytool submit "$OUTPUT_PKG" \
    --keychain-profile "$NOTARY_PROFILE" \
    --wait
  xcrun stapler staple "$OUTPUT_PKG"
  xcrun stapler validate "$OUTPUT_PKG"
  spctl --assess --type install --verbose=2 "$OUTPUT_PKG"
fi

echo "Created: $OUTPUT_PKG"
if [[ "$UNSIGNED" -eq 1 ]]; then
  echo "WARNING: This package is unsigned and is only suitable for local testing."
fi
