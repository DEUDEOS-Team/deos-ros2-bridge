#!/bin/bash
set -Eeuo pipefail
export GIT_TERMINAL_PROMPT=0
base=/home/deos/deos-bridge-updater
source_dir="$base/source"
params=/home/deos/deos_ros_bridge/params.yaml
container=deos-ros-bridge
mkdir -p "$base"
exec 9>"$base/update.lock"
flock -n 9 || exit 0
if [ ! -d "$source_dir/.git" ]; then
    git clone --no-checkout https://github.com/DEUDEOS-Team/deos-ros2-bridge.git "$source_dir"
fi
git -C "$source_dir" fetch origin main
revision=$(git -C "$source_dir" rev-parse FETCH_HEAD)
if [ -f "$base/deployed-revision" ] && [ "$(cat "$base/deployed-revision")" = "$revision" ]; then
    echo "Main unchanged: $revision"
    exit 0
fi
test -s "$params"
git -C "$source_dir" checkout --detach "$revision"
git -C "$source_dir" submodule sync --recursive
git -C "$source_dir" submodule update --init --recursive
bash "$source_dir/ops/test-unit.sh"
image="deos-ros-bridge:main-$revision"
docker build --pull --label "org.opencontainers.image.revision=$revision" -t "$image" "$source_dir"
docker run --rm --entrypoint /bin/bash "$image" -c \
    'source /opt/ros/jazzy/setup.bash && source /opt/deos/local_setup.bash && ros2 pkg executables deos_ros_bridge | grep -q deos_ros_bridge_node'

backup="deos-ros-bridge-previous"
had_previous=false
was_running=false
switched=false
restore() {
    result=$?
    if [ "$switched" = true ]; then
        docker logs --tail 60 "$container" || true
        docker rm -f "$container" || true
        if [ "$had_previous" = true ]; then
            docker rename "$backup" "$container"
            docker update --restart=unless-stopped "$container" >/dev/null
            if [ "$was_running" = true ]; then docker start "$container"; fi
        fi
    fi
    exit "$result"
}
trap restore ERR INT TERM
if docker container inspect "$container" >/dev/null 2>&1; then
    was_running=$(docker inspect -f '{{.State.Running}}' "$container")
    docker rm -f "$backup" >/dev/null 2>&1 || true
    docker rename "$container" "$backup"
    had_previous=true
    switched=true
    docker update --restart=no "$backup" >/dev/null
    docker stop -t 10 "$backup" >/dev/null
else
    switched=true
fi
docker run -d --name "$container" --init --network host --restart unless-stopped \
    -e ROS_DOMAIN_ID=0 \
    --mount "type=bind,source=$params,target=/config/params.yaml,readonly" \
    "$image" ros2 launch deos_ros_bridge bridge.launch.py params_file:=/config/params.yaml
for attempt in $(seq 1 30); do
    if [ "$(docker inspect -f '{{.State.Running}}' "$container")" != true ]; then false; fi
    if docker logs "$container" 2>&1 | grep -q 'DEOS ROS 2 Bridge Node initialized'; then break; fi
    sleep 1
done
docker logs "$container" 2>&1 | grep -q 'DEOS ROS 2 Bridge Node initialized'
sleep 10
test "$(docker inspect -f '{{.State.Running}}' "$container")" = true
test "$(docker inspect -f '{{.RestartCount}}' "$container")" = 0
docker exec "$container" pgrep -f /opt/deos/lib/deos_ros_bridge/deos_ros_bridge_node >/dev/null
printf '%s\n' "$revision" > "$base/deployed-revision.tmp"
mv "$base/deployed-revision.tmp" "$base/deployed-revision"
switched=false
trap - ERR INT TERM
echo "Deployed main $revision successfully"
