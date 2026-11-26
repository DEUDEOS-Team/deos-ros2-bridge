#!/bin/bash
set -euo pipefail
test "$(id -u)" = 0 || { echo 'Run with sudo'; exit 1; }
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
python3 -c 'import yaml'
test -f /home/deos/deos-bridge-updater/update-bridge.sh
systemctl cat deos-bridge-update.service >/dev/null
install -d -m 755 /etc/deos /usr/local/lib/deos /var/lib/deos
install -m 644 "$repo/ops/mode.py" /usr/local/lib/deos/mode.py
install -m 755 "$repo/ops/deos-mode" /usr/local/bin/deos-mode
install -m 644 "$repo/ops/deos-developer.service" /etc/systemd/system/
if [ ! -e /etc/deos/system.yaml ]; then
    install -m 644 "$repo/ops/system.yaml" /etc/deos/system.yaml
fi
# The existing updater unit continues to own any in-progress deployment.
systemctl disable --now deos-bridge-update.timer
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/deos-developer.service
systemctl enable deos-developer.service
systemctl restart deos-developer.service
for attempt in $(seq 1 30); do
    if [ -S /run/deos/control.sock ] && deos-mode status; then exit 0; fi
    sleep 1
done
echo 'Mode manager failed to start; restoring update timer' >&2
systemctl disable --now deos-developer.service
systemctl enable --now deos-bridge-update.timer
exit 1
