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
config=$(mktemp -d)
network=
mock=
bridge=
stm32=
cleanup() {
    if [ -n "$bridge" ]; then docker logs "$bridge"; docker stop -t 2 "$bridge" >/dev/null; fi
    if [ -n "$mock" ]; then docker logs "$mock"; docker stop -t 2 "$mock" >/dev/null; fi
    if [ -n "$stm32" ]; then docker logs "$stm32"; docker stop -t 2 "$stm32" >/dev/null; fi
    if [ -n "$network" ]; then docker network rm "$network" >/dev/null; fi
    if [ -f "$config/params.yaml" ]; then unlink "$config/params.yaml"; fi
    rmdir "$config"
}
trap cleanup EXIT
network=$(docker network create --internal "deos-e2e-$$")
mock_args=()
if [ "$reject_imu" = 1 ]; then mock_args+=(--reject-imu); fi
stm32=$(docker run -d --rm --init --network "$network" \
  -v "$repo:/fixture:ro" --entrypoint python3 "$mock_image" \
  /fixture/test/mock_stm32.py --bind 0.0.0.0 "${mock_args[@]}")
for attempt in $(seq 1 100); do
    if docker logs "$stm32" 2>&1 | grep -q "UDP test peer ready"; then break; fi
    sleep .1
done
docker logs "$stm32" 2>&1 | grep -q "UDP test peer ready"
stm32_ip=$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$stm32")
test -n "$stm32_ip"
sed -E "s/(udp_remote_ip: )[^ ]+/\1\"$stm32_ip\"/" "$repo/config/params.yaml" > "$config/params.yaml"
mock=$(docker run -d --rm --init --network "$network" -e ROS_DOMAIN_ID=42 \
  -e FASTDDS_BUILTIN_TRANSPORTS=UDPv4 "$mock_image" --enable-commands)
bridge=$(docker run -d --rm --init --network "$network" -e ROS_DOMAIN_ID=42 \
  -e FASTDDS_BUILTIN_TRANSPORTS=UDPv4 \
  --mount "type=bind,src=$config/params.yaml,dst=/config/params.yaml,readonly" \
  "$bridge_image" ros2 launch deos_ros_bridge bridge.launch.py params_file:=/config/params.yaml)
docker exec -i "$mock" python3 - "$reject_imu" <<'PY'
import json
import sys
import time
import urllib.request
base = "http://127.0.0.1:8765"
topic = "/vehicle/lights/headlight"
imu_topic = "/sensors/imu/data"
ping_topic = "/deos/network/ping"
pong_topic = "/deos/network/pong"
lighting_topic = "/deos/lighting/response"
reject = sys.argv[1] == "1"
for attempt in range(120):
    try:
        status = json.load(urllib.request.urlopen(base + "/api/status", timeout=1))
        if (status["commands"].get(topic) and status["commands"].get(ping_topic)
                and status["telemetry"].get(pong_topic) and status["telemetry"].get(lighting_topic) and (reject or imu_topic in status["latest"])):
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
assert event["direction"] == "tx" and event["group"] == "commands" and event["message"]["data"] is True
ping_id = 0x10203040
request = urllib.request.Request(base + "/api/command", json.dumps({"topic": ping_topic, "value": ping_id}).encode(),
                                 {"Origin": base, "Content-Type": "application/json"})
assert json.load(urllib.request.urlopen(request))["message"]["data"] == ping_id
for attempt in range(24):
    status = json.load(urllib.request.urlopen(base + "/api/status", timeout=1))
    if status["latest"].get(pong_topic, {}).get("message", {}).get("data") == ping_id:
        break
    time.sleep(.25)
else:
    raise SystemExit("No matching PONG received")
ack = status["latest"][lighting_topic]
assert ack["group"] == "responses"
assert ack["message"]["command"] == "TURN_ON"
assert ack["message"]["result_code"] == 0 and ack["message"]["headlight"] == 1
print("HTTP: IMU behavior, light response, and matching PONG verified")
PY
for attempt in $(seq 1 12); do
    if docker logs "$stm32" 2>&1 | grep -q 'Cmd:TURN_ON'; then break; fi
    sleep .25
done
logs=$(docker logs "$stm32" 2>&1)
bridge_logs=$(docker logs "$bridge" 2>&1)
test "$(grep -c 'Cmd:SUBSCRIBE' <<<"$logs")" = 1
grep -Eq 'Cmd:PONG .*Payload:12345678' <<<"$logs"
test "$(grep -c 'Cmd:PING' <<<"$logs")" = 1
grep -Eq 'Cmd:PING .*Payload:40302010' <<<"$logs"
grep -Eq 'Cmd:TURN_ON .*Payload:01' <<<"$logs"
if [ "$reject_imu" = 1 ]; then
    grep -q 'STM32 rejected IMU subscription (result 1)' <<<"$bridge_logs"
else
    grep -q 'STM32 accepted IMU subscription' <<<"$bridge_logs"
fi
if grep -q 'Cmd:HEARTBEAT' <<<"$logs"; then exit 1; fi
echo "UDP: SUBSCRIBE, PONG, and browser-driven TURN_ON verified"
