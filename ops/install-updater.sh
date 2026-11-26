#!/bin/bash
set -euo pipefail
test "$(id -u)" = 0 || { echo 'Run with sudo'; exit 1; }
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
base=/opt/deos/bridge-updater
install -d -o deos -g deos -m 755 "$base"
exec 9>"$base/update.lock"
chown deos:deos "$base/update.lock"
flock -x 9
install -m 755 "$repo/ops/update-bridge.sh" "$base/update-bridge.sh.new"
mv "$base/update-bridge.sh.new" "$base/update-bridge.sh"
install -m 644 "$repo/ops/deos-bridge-update.service" /etc/systemd/system/
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/deos-bridge-update.service
echo 'Bridge updater installed'
