#!/bin/bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
cmake -S "$repo/vendor/deos_protocol" -B "$build/protocol" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build/protocol" --parallel 2
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread \
    -I"$repo/include" -I"$repo/vendor/deos_protocol/include" \
    "$repo/test/unit_tests.cpp" "$repo/src/kinematics.cpp" \
    "$repo/src/safety_supervisor.cpp" "$repo/src/deos_codec.cpp" \
    "$repo/src/message_dispatcher.cpp" "$repo/src/universal_frame.cpp" \
    "$build/protocol/libdeos_protocol.a" -o "$build/unit-tests"
"$build/unit-tests"
