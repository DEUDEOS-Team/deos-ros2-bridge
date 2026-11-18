#!/bin/bash
set -eu
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bridge_image=${BRIDGE_IMAGE:-deos-ros-bridge:universal-amd64}
mock_image=${MOCK_IMAGE:-deos-ros-mock:jazzy}
reject_imu=0
if [ "${1:-}" = "--reject-imu" ]; then
    reject_imu=1
elif [ "$#" -ne 0 ]; then
    echo "Usage: bash test/docker_e2e.sh [--reject-imu]" >&2
    exit 2
fi
network=$(docker network create --internal "deos-e2e-$$")
mock=
bridge=
cleanup() {
    if [ -n "$bridge" ]; then docker logs "$bridge"; docker stop -t 2 "$bridge" >/dev/null; fi
    if [ -n "$mock" ]; then docker logs "$mock"; docker stop -t 2 "$mock" >/dev/null; fi
    docker network rm "$network" >/dev/null
}
trap cleanup EXIT
mock=$(docker run -d --rm --init --network "$network" -e ROS_DOMAIN_ID=42 \
  -e FASTDDS_BUILTIN_TRANSPORTS=UDPv4 "$mock_image" --enable-commands)
bridge=$(docker run -d --rm --init --network "$network" -e ROS_DOMAIN_ID=42 \
  -e FASTDDS_BUILTIN_TRANSPORTS=UDPv4 \
  -e REJECT_IMU="$reject_imu" -v "$repo:/fixture:ro" "$bridge_image" bash -c '
    mock_args=()
    if [ "$REJECT_IMU" = 1 ]; then mock_args+=(--reject-imu); fi
    python3 /fixture/test/mock_stm32.py "${mock_args[@]}" &
    exec ros2 run deos_ros_bridge deos_ros_bridge_node --ros-args -p udp_remote_ip:=127.0.0.1
')
docker exec -i "$mock" python3 - "$reject_imu" <<'PY'
import json
import sys
import time
import urllib.request
base = "http://127.0.0.1:8765"
topic = "/vehicle/lights/headlight"
imu_topic = "/sensors/imu/data"
reject = sys.argv[1] == "1"
for attempt in range(60):
    try:
        status = json.load(urllib.request.urlopen(base + "/api/status", timeout=1))
        if status["commands"].get(topic) and (reject or imu_topic in status["latest"]):
            break
    except OSError:
        pass
    time.sleep(.25)
else:
    raise SystemExit("No matched light subscriber or IMU samples")
if reject:
    time.sleep(1)
    status = json.load(urllib.request.urlopen(base + "/api/status", timeout=1))
    assert imu_topic not in status["latest"], "IMU data present after rejected subscription"
else:
    imu = status["latest"][imu_topic]["message"]
    assert imu["header"]["frame_id"] == "imu_link"
    assert abs(imu["linear_acceleration"]["z"] - 9.81) < 0.001
request = urllib.request.Request(base + "/api/command", json.dumps({"topic": topic, "value": True}).encode(),
                                 {"Origin": base, "Content-Type": "application/json"})
event = json.load(urllib.request.urlopen(request))
assert event["direction"] == "tx" and event["message"]["data"] is True
print("HTTP: expected IMU behavior and light command TX verified")
PY
for attempt in $(seq 1 12); do
    if docker logs "$bridge" 2>&1 | grep -q 'Cmd:TURN_ON'; then break; fi
    sleep .25
done
logs=$(docker logs "$bridge" 2>&1)
test "$(grep -c 'Cmd:SUBSCRIBE' <<<"$logs")" = 1
grep -Eq 'Cmd:PONG .*Payload:12345678' <<<"$logs"
grep -Eq 'Cmd:TURN_ON .*Payload:01' <<<"$logs"
if [ "$reject_imu" = 1 ]; then
    grep -q 'STM32 rejected IMU subscription (result 1)' <<<"$logs"
else
    grep -q 'STM32 accepted IMU subscription' <<<"$logs"
fi
if grep -q 'Cmd:HEARTBEAT' <<<"$logs"; then exit 1; fi
echo "UDP: SUBSCRIBE, PONG, and browser-driven TURN_ON verified"
