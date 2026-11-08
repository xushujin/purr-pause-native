#!/usr/bin/env bash
set -euo pipefail
# Run inside dbus-run-session and xvfb-run so no user's desktop is affected.
# The mock login1 service is confined to this private bus as well.
export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"
openbox > /dev/null 2>&1 &
window_manager_pid=$!
trap 'kill "$window_manager_pid" 2>/dev/null || true' EXIT
"$@"
