#!/bin/bash
set -e

source /opt/ros/jazzy/setup.bash
source /opt/deos/local_setup.bash
exec "$@"
