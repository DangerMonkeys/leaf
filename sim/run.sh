#!/bin/sh
# Builds and runs the Leaf device emulator in a container.  See run.ps1 for the Windows equivalent.
#
#   ./sim/run.sh                                  interactive, control panel on :8080
#   ./sim/run.sh --scenario sim/recordings/x.igc  load and play a recording at startup
#
# Anything after `--` is passed straight to leafsim, so the invocations in README.md work here:
#
#   ./sim/run.sh -- --setting LAB_THERM_TRACK=1
set -e

PORT=8080
SPEED=1
VARIANT=leaf_3_2_7
SCENARIO=""
EXTRA=""

while [ $# -gt 0 ]; do
  case "$1" in
    --port) PORT="$2"; shift 2 ;;
    --speed) SPEED="$2"; shift 2 ;;
    --variant) VARIANT="$2"; shift 2 ;;
    --scenario) SCENARIO="$2"; shift 2 ;;
    --) shift; EXTRA="$*"; break ;;
    *) echo "unknown option: $1 (pass leafsim options after --)"; exit 2 ;;
  esac
done

ROOT=$(cd "$(dirname "$0")/.." && pwd)

ARGS="--port $PORT --speed $SPEED"
[ -n "$SCENARIO" ] && ARGS="$ARGS --scenario $SCENARIO --play"
[ -n "$EXTRA" ] && ARGS="$ARGS $EXTRA"

echo "Building the Leaf emulator (first build takes a few minutes)..."
exec docker run --rm -it \
  -p "$PORT:$PORT" \
  -p "7431:7431/udp" \
  -v "$ROOT:/leaf" \
  -w /leaf \
  gcc:13 sh -c "make -C sim VARIANT=$VARIANT -j\$(nproc) && ./sim/build/leafsim $ARGS"
