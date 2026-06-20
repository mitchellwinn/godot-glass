#!/usr/bin/env bash
# Build the Godot Glass editor (macOS, arm64) and install it to /Applications.
#
#   ./build_and_install_mac.sh
#
# First build ~15-25 min; incremental rebuilds after small changes ~1-3 min
# (scons only recompiles what changed). This is the on-demand "I wanna test"
# path — no CI needed. CI (tags) is reserved for releases (Windows + auto-updater).
set -euo pipefail
cd "$(dirname "$0")"

echo "==> Pulling latest glass..."
git pull --ff-only 2>/dev/null || echo "    (skipped pull)"

# ANGLE gives the editor a GL Compatibility renderer on macOS (no modern native GL).
# Run once; marker avoids re-downloading every build.
if [ ! -f .glass_angle_done ]; then
  echo "==> Installing ANGLE (one-time)..."
  python misc/scripts/install_angle.py && touch .glass_angle_done
fi

JOBS=$(sysctl -n hw.ncpu)
echo "==> Building editor (arm64, $JOBS jobs)..."
scons platform=macos target=editor arch=arm64 vulkan=no accesskit=no angle=yes -j"$JOBS"

BIN=bin/godot.macos.editor.arm64
[ -f "$BIN" ] || { echo "!! no binary at $BIN"; ls bin; exit 1; }

APP="/Applications/Godot Glass.app"
echo "==> Installing -> $APP"
rm -rf "$APP"
cp -R misc/dist/macos_tools.app "$APP"
mkdir -p "$APP/Contents/MacOS"
cp "$BIN" "$APP/Contents/MacOS/Godot"
chmod +x "$APP/Contents/MacOS/Godot"
codesign --force --deep -s - "$APP" 2>/dev/null || true
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

echo "==> Done. Launch with:  open -a 'Godot Glass'"
