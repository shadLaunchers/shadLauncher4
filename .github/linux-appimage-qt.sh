#!/bin/bash
# SPDX-FileCopyrightText: 2026 shadLauncher4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [[ -z "${GITHUB_WORKSPACE:-}" ]]; then
    GITHUB_WORKSPACE="${PWD%/*}"
fi

export EXTRA_QT_PLUGINS="waylandcompositor"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"
export OUTPUT="shadLauncher4.AppImage"

echo "Workspace: $GITHUB_WORKSPACE"

wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-checkrt/releases/download/continuous/linuxdeploy-plugin-checkrt-x86_64.sh

chmod +x linuxdeploy-x86_64.AppImage
chmod +x linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-plugin-checkrt-x86_64.sh

if command -v qtpaths >/dev/null 2>&1; then
    QT_PLUGIN_DIR="$(qtpaths --plugin-dir)"

    echo "Qt plugin directory: $QT_PLUGIN_DIR"

    if [[ -d "$QT_PLUGIN_DIR/sqldrivers" ]]; then
        echo "Installed SQL drivers:"
        ls -la "$QT_PLUGIN_DIR/sqldrivers"

        rm -f "$QT_PLUGIN_DIR/sqldrivers/libqsqlmimer.so"
        rm -f "$QT_PLUGIN_DIR/sqldrivers/libqsqlmysql.so"
        rm -f "$QT_PLUGIN_DIR/sqldrivers/libqsqlpsql.so"
        rm -f "$QT_PLUGIN_DIR/sqldrivers/libqsqlodbc.so"

        echo "Remaining SQL drivers:"
        ls -la "$QT_PLUGIN_DIR/sqldrivers"
    fi
fi

./linuxdeploy-x86_64.AppImage --appdir AppDir
./linuxdeploy-plugin-checkrt-x86_64.sh --appdir AppDir

mkdir -p AppDir/usr/bin

if [[ -d "$GITHUB_WORKSPACE/build/translations" ]]; then
    cp -a "$GITHUB_WORKSPACE/build/translations" AppDir/usr/bin/
fi

./linuxdeploy-x86_64.AppImage \
    --appdir AppDir \
    -d "$GITHUB_WORKSPACE/dist/net.shadps4.shadLauncher4.desktop" \
    -e "$GITHUB_WORKSPACE/build/shadLauncher4" \
    -i "$GITHUB_WORKSPACE/src/images/shadLauncher4.png" \
    --plugin qt

# Optional multimedia plugin removal
rm -f AppDir/usr/plugins/multimedia/libgstreamermediaplugin.so

./linuxdeploy-x86_64.AppImage \
    --appdir AppDir \
    --output appimage

echo "AppImage successfully created: $OUTPUT"