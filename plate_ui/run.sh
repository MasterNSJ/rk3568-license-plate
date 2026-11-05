#!/bin/sh
set -eu
export QT_QPA_PLATFORM=wayland
export XDG_RUNTIME_DIR=/var/run
export QT_QPA_FONTDIR=/usr/share/fonts
cd "$(dirname "$0")"
exec ./plate_ui "$@"
