#!/bin/bash
# SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Kyty-021: Linux AppImage packaging script.
#
# Creates a portable AppImage from the CI build output.
# The AppImage runs on Ubuntu 22.04+ without installing dependencies.
#
# Usage:
#   bash packaging/linux/create-appimage.sh _Build/linux/install AbdoPS5-Linux-x64.AppImage
#
# Requirements:
#   - appimagetool (downloaded automatically if not found)
#   - The build output must contain: launcher, kyty_emulator, data/, tools/

set -e

INSTALL_DIR="${1:-_Build/linux/install}"
OUTPUT="${2:-AbdoPS5-Linux-x64.AppImage}"
APPDIR="AppDir"

echo "=== Creating AppImage from $INSTALL_DIR ==="

# Clean up any previous build
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APP_DIR/usr/share/applications"

# Copy binaries
cp "$INSTALL_DIR/launcher" "$APPDIR/usr/bin/"
cp "$INSTALL_DIR/kyty_emulator" "$APPDIR/usr/bin/"

# Copy tools (if they exist)
if [ -d "$INSTALL_DIR/tools" ]; then
    mkdir -p "$APPDIR/usr/bin/tools"
    cp "$INSTALL_DIR/tools/"* "$APPDIR/usr/bin/tools/" 2>/dev/null || true
fi

# Copy data directory
if [ -d "$INSTALL_DIR/data" ]; then
    mkdir -p "$APPDIR/usr/share/abdops5"
    cp -r "$INSTALL_DIR/data" "$APPDIR/usr/share/abdops5/"
fi

# Copy scripts
if [ -d "$INSTALL_DIR/scripts" ]; then
    mkdir -p "$APPDIR/usr/share/abdops5/scripts"
    cp "$INSTALL_DIR/scripts/"* "$APPDIR/usr/share/abdops5/scripts/" 2>/dev/null || true
fi

# Copy shared libraries that the launcher needs (Qt6, SDL2, etc.)
# We use linuxdeploy to handle this automatically
for lib in $(ldd "$INSTALL_DIR/launcher" 2>/dev/null | grep "=>" | awk '{print $3}' | grep -v "^/lib" | grep -v "^/usr/lib/x86_64-linux-gnu"); do
    if [ -f "$lib" ]; then
        echo "  Bundling: $(basename $lib)"
        cp "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
    fi
done

# Create AppRun entry point
cat > "$APPDIR/AppRun" << 'APPRUN'
#!/bin/bash
export APPDIR=$(dirname "$0")
export LD_LIBRARY_PATH="$APPDIR/usr/lib:$LD_LIBRARY_PATH"
export APPDATA="$APPDIR/usr/share/abdops5"
export PATH="$APPDIR/usr/bin:$PATH"
cd "$APPDIR"
exec "$APPDIR/usr/bin/launcher" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# Create .desktop file
cat > "$APPDIR/abdops5.desktop" << 'DESKTOP'
[Desktop Entry]
Type=Application
Name=AbdoPS5
Comment=PS5 Emulator
Exec=AppRun
Icon=abdops5
Terminal=false
Categories=Game;Emulator;
DESKTOP

# Create icon (placeholder — use the Qt6 icon if available)
if [ -f "$INSTALL_DIR/launcher.png" ]; then
    cp "$INSTALL_DIR/launcher.png" "$APPDIR/abdops5.png"
else
    # Create a simple placeholder icon
    convert -size 256x256 xc:blue "$APPDIR/abdops5.png" 2>/dev/null || true
fi

# Download appimagetool if not installed
if ! command -v appimagetool &> /dev/null; then
    echo "Downloading appimagetool..."
    wget -q -O /tmp/appimagetool "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x /tmp/appimagetool
    APPIMAGETOOL=/tmp/appimagetool
else
    APPIMAGETOOL=appimagetool
fi

# Build the AppImage
echo "Building AppImage..."
"$APPIMAGETOOL" "$APPDIR" "$OUTPUT" 2>&1 || {
    echo "appimagetool failed — falling back to simple tar-based AppImage"
    # Fallback: create a self-extracting archive
    tar czf - -C "$APPDIR" . > /tmp/appimage.tar.gz
    cat > "$OUTPUT" << 'HEADER'
#!/bin/bash
# Simple AppImage fallback
TMPDIR=$(mktemp -d)
tail -n +3 "$0" | tar xzf - -C "$TMPDIR"
cd "$TMPDIR"
exec ./AppRun "$@"
exit 0
HEADER
    cat /tmp/appimage.tar.gz >> "$OUTPUT"
    chmod +x "$OUTPUT"
    rm -f /tmp/appimage.tar.gz
}

chmod +x "$OUTPUT"
echo "=== Created $OUTPUT ($(du -h "$OUTPUT" | cut -f1)) ==="

# Clean up
rm -rf "$APPDIR"
