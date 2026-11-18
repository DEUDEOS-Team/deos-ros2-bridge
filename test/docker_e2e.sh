#!/bin/bash
set -eu
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bridge_image=${BRIDGE_IMAGE:-deos-ros-bridge:universal-amd64}
mock_image=${MOCK_IMAGE:-deos-ros-mock:jazzy}
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
  -v "$repo:/fixture:ro" "$bridge_image" bash -c '
    python3 /fixture/test/mock_stm32.py &
    exec ros2 run deos_ros_bridge deos_ros_bridge_node --ros-args -p udp_remote_ip:=127.0.0.1
')
docker exec -i "$mock" python3 - <<'PY'
import json
import time
import urllib.error
import urllib.request
base = "http://127.0.0.1:8765"
topic = "/vehicle/lights/headlight"
for attempt in range(60):
    try:
        status = json.load(urllib.request.urlopen(base + "/api/status", timeout=1))
        if topic in status["commands"] and status["commands"][topic] and "/sensors/imu/data" in status["latest"]:
            break
    except OSError:
        pass
    time.sleep(.25)
else:
    raise SystemExit("No matched light subscriber or IMU samples")
imu = status["latest"]["/sensors/imu/data"]["message"]
assert imu["header"]["frame_id"] == "imu_link"
assert abs(imu["linear_acceleration"]["z"] - 9.81) < 0.001
request = urllib.request.Request(base + "/api/command", json.dumps({"topic": topic, "value": True}).encode(),
                                 {"Origin": base, "Content-Type": "application/json"})
event = json.load(urllib.request.urlopen(request))
assert event["direction"] == "tx" and event["message"]["data"] is True
print("HTTP: IMU RX and light command TX verified")
PY
for attempt in $(seq 1 12); do
    if docker logs "$bridge" 2>&1 | grep -q 'Cmd:TURN_ON'; then break; fi
    sleep .25
done
logs=$(docker logs "$bridge" 2>&1)
test "$(grep -c 'Cmd:SUBSCRIBE' <<<"$logs")" = 1
grep -Eq 'Cmd:PONG .*Payload:12345678' <<<"$logs"
grep -Eq 'Cmd:TURN_ON .*Payload:01' <<<"$logs"
grep -q 'STM32 accepted IMU subscription' <<<"$logs"
if grep -q 'Cmd:HEARTBEAT' <<<"$logs"; then exit 1; fi
echo "UDP: SUBSCRIBE, PONG, and browser-driven TURN_ON verified"
