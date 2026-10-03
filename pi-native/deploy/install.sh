#!/usr/bin/env bash
# Build and install the Image Tunnel kiosk on Raspberry Pi OS Bookworm (64-bit).
#
#   ./deploy/install.sh http://192.168.1.10/            # build, install, enable the service
#   ./deploy/install.sh http://192.168.1.10/ --kiosk    # also stop booting into the desktop
#
set -euo pipefail

SOURCE="${1:-}"
KIOSK=0
[[ "${2:-}" == "--kiosk" ]] && KIOSK=1
if [[ -z "$SOURCE" ]]; then
    echo "usage: $0 <nginx base URL or local web-root dir> [--kiosk]" >&2
    exit 64
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RUN_USER="${SUDO_USER:-$USER}"

echo "==> Installing build dependencies"
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config git \
    libsdl2-dev libgles-dev libegl-dev libwebp-dev libcurl4-openssl-dev \
    nlohmann-json3-dev libglm-dev

echo "==> Building"
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/imagetunnel
cmake --build "$ROOT/build"
ctest --test-dir "$ROOT/build" --output-on-failure

echo "==> Installing to /opt/imagetunnel"
sudo cmake --install "$ROOT/build"

echo "==> Configuring service for user $RUN_USER"
sudo usermod -aG video,render,input "$RUN_USER"
printf 'IMAGETUNNEL_SOURCE=%s\nIMAGETUNNEL_ARGS=\n' "$SOURCE" | sudo tee /etc/default/imagetunnel >/dev/null
sed "s/@USER@/$RUN_USER/" "$ROOT/deploy/imagetunnel.service" | sudo tee /etc/systemd/system/imagetunnel.service >/dev/null
sudo systemctl daemon-reload
sudo systemctl enable imagetunnel.service

if [[ $KIOSK -eq 1 ]]; then
    echo "==> Switching default boot target to console (no desktop)"
    sudo systemctl set-default multi-user.target
    # The unit conflicts with the tty1 login prompt; if both are enabled systemd drops the viewer at
    # boot. Logins remain available on other VTs (Ctrl+Alt+F2) and over SSH.
    sudo systemctl disable getty@tty1.service
fi

cat <<EOF

Installed. Next steps:
  * Reboot (group changes and the boot target take effect then), or start now with:
        sudo systemctl start imagetunnel
  * Logs:        journalctl -u imagetunnel -f
  * Extra args:  edit IMAGETUNNEL_ARGS in /etc/default/imagetunnel (e.g. --size 1920x1080 --refresh 60)
  * The desktop must not be running: the app needs to own the display (KMS/DRM).
EOF
