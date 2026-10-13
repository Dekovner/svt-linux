#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${HOME}/.local"

mkdir -p "${PREFIX}/bin"
mkdir -p "${PREFIX}/share/icons/hicolor/64x64/apps"
mkdir -p "${PREFIX}/share/pixmaps"
mkdir -p "${PREFIX}/share/applications"

install -m 755 "${DIR}/svt" "${PREFIX}/bin/svt"
install -m 644 "${DIR}/svt.png" "${PREFIX}/share/icons/hicolor/64x64/apps/svt.png"
install -m 644 "${DIR}/svt.png" "${PREFIX}/share/pixmaps/svt.png"

cat > "${PREFIX}/share/applications/svt.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=SVT
Comment=osu! Slider Velocity & Volume Tool (v1.0.3)
Exec=${PREFIX}/bin/svt
Icon=svt
Terminal=false
Categories=Utility;
StartupWMClass=svt
EOF

update-desktop-database "${PREFIX}/share/applications" 2>/dev/null || true
echo "[SVT] Installed to ${PREFIX}/bin/svt and added to application menu (svt.desktop)!"
