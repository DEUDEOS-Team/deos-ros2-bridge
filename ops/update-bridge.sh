#!/bin/bash
set -Eeuo pipefail
export GIT_TERMINAL_PROMPT=0
base=/opt/deos/bridge-updater
source_dir="$base/source"
container=deos-ros-bridge
backup=deos-ros-bridge-previous
exec 9>"$base/update.lock"
flock -n 9 || exit 0
git -C "$source_dir" fetch origin main
revision=$(git -C "$source_dir" rev-parse FETCH_HEAD)
if [ -f "$base/deployed-revision" ] && [ "$(cat "$base/deployed-revision")" = "$revision" ]; then
    echo "Main unchanged: $revision"
    exit 0
fi
ci=$(python3 - "$revision" <<'PY'
import json, sys, urllib.request
sha = sys.argv[1]
url = 'https://api.github.com/repos/DEUDEOS-Team/deos-ros2-bridge/actions/workflows/ci.yml/runs'
request = urllib.request.Request(url + '?head_sha=' + sha + '&event=push&per_page=20',
                                 headers={'User-Agent': 'DEOS-Updater'})
with urllib.request.urlopen(request, timeout=30) as response:
    runs = [r for r in json.load(response)['workflow_runs']
            if r['head_sha'] == sha and r['head_branch'] == 'main' and r['event'] == 'push']
latest = max(runs, key=lambda r: r['id']) if runs else {}
print(latest.get('conclusion') if latest.get('status') == 'completed' else 'pending')
PY
)
if [ "$ci" != success ]; then echo "CI $ci: update deferred"; exit 0; fi
git -C "$source_dir" checkout --detach "$revision"
git -C "$source_dir" submodule update --init --recursive
bash "$source_dir/ops/test-unit.sh"
export BRIDGE_IMAGE="deos-ros-bridge:main-$revision" BRIDGE_REVISION="$revision"
compose=(docker compose --env-file /dev/null -p "deos-bridge-${revision:0:12}" -f "$source_dir/deploy/compose.yaml")
"${compose[@]}" config --quiet
"${compose[@]}" build --pull bridge
old_image=$(docker inspect -f '{{.Image}}' "$container")
old_running=$(docker inspect -f '{{.State.Running}}' "$container")
old_restart=$(docker inspect -f '{{.HostConfig.RestartPolicy.Name}}' "$container")
docker rm -f "$backup" >/dev/null 2>&1 || true
docker rename "$container" "$backup"
restore() {
    result=$?
    trap - ERR INT TERM
    docker logs --tail 30 "$container" || true
    docker rm -f "$container" || true
    docker rename "$backup" "$container"
    docker update --restart="$old_restart" "$container" >/dev/null
    if [ "$old_running" = true ]; then docker start "$container"; fi
    exit "$result"
}
trap restore ERR INT TERM
docker update --restart=no "$backup" >/dev/null
docker stop -t 10 "$backup" >/dev/null
"${compose[@]}" up -d --no-build --pull never --wait --wait-timeout 60 bridge
if [ -f "$base/current-compose.yaml" ]; then
    cp "$base/current-compose.yaml" "$base/previous-compose.yaml"
fi
cp "$source_dir/deploy/compose.yaml" "$base/current-compose.yaml"
printf '%s\n' "$revision" > "$base/deployed-revision.tmp"
mv "$base/deployed-revision.tmp" "$base/deployed-revision"
trap - ERR INT TERM
current_image=$(docker inspect -f '{{.Image}}' "$container")
while read -r image; do
    id=$(docker image inspect -f '{{.Id}}' "$image")
    if [ "$id" = "$current_image" ] || [ "$id" = "$old_image" ]; then continue; fi
    docker image rm "$image" >/dev/null 2>&1 || true
done < <(docker image ls deos-ros-bridge --format '{{.Repository}}:{{.Tag}}' | grep ':main-')
echo "Deployed main $revision"
