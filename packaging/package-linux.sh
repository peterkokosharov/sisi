#!/usr/bin/env bash
# Builds dist/sisi-linux-x86_64.tar.gz from the built binary: the program, the Qt 6
# libraries it needs (dynamically linked, LGPLv3) and the Qt plugins, plus a launcher.
# Usage: packaging/package-linux.sh build/sisi      (run from the repository root)
set -euo pipefail

BIN=${1:?path to the sisi binary}
OUT=dist/sisi-linux
rm -rf dist && mkdir -p "$OUT/lib" "$OUT/plugins"

cp "$BIN" "$OUT/sisi.bin"
cp README.md LICENSE LICENSE-ini THIRD-PARTY.txt sisi.ini.example "$OUT/"
cp -r packaging/icons "$OUT/icons"
cp packaging/icons/sisi.desktop "$OUT/sisi.desktop"
cp packaging/INSTALL-linux.txt "$OUT/INSTALL.txt"
rm -f "$OUT/icons/README.md" "$OUT/icons/sisi.ico" "$OUT/icons/sisi.icns" "$OUT/icons/sisi.desktop"

# Qt libraries from the Qt installation (not the system ones).
QT_ROOT=${QT_ROOT_DIR:?run this after install-qt-action (it sets QT_ROOT_DIR)}
ldd "$BIN" | awk '/=> \//{print $3}' | grep "^$QT_ROOT/" | while read -r lib; do
    cp -L "$lib" "$OUT/lib/"
done
# libicu (ships with the Qt install) and the platform-related libraries Qt loads at run time.
cp -L "$QT_ROOT"/lib/libicu*.so* "$OUT/lib/" 2>/dev/null || true
cp -L "$QT_ROOT"/lib/libQt6{XcbQpa,DBus}.so.6 "$OUT/lib/" 2>/dev/null || true

# Plugins: platform (xcb), xcb GL integrations, SVG icon + image support, styles.
for d in platforms xcbglintegrations imageformats iconengines platformthemes; do
    [ -d "$QT_ROOT/plugins/$d" ] && cp -r "$QT_ROOT/plugins/$d" "$OUT/plugins/"
done

cat > "$OUT/sisi" <<'LAUNCH'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$here/plugins"
exec "$here/sisi.bin" "$@"
LAUNCH
chmod +x "$OUT/sisi" "$OUT/sisi.bin"

(cd dist && tar czf sisi-linux-x86_64.tar.gz sisi-linux)
echo "dist/sisi-linux-x86_64.tar.gz written"
