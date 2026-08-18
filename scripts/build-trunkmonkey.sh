#!/bin/sh
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# Compatibility wrapper retained for 1.0 users/scripts.
# With no arguments it performs the old behavior: build CLI + GUI without
# installing them system-wide. Any supplied arguments are forwarded directly
# to the new WaffleHouse-style top-level builder.
if [ "$#" -eq 0 ]; then
  exec "$ROOT_DIR/build.sh" --all --no-install
fi
exec "$ROOT_DIR/build.sh" "$@"
