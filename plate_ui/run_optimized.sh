#!/bin/sh
# Experiment-derived preset. Retain run.sh and all environment overrides.
set -eu
export PLATE_FRAME_FORMAT=${PLATE_FRAME_FORMAT:-bmp}
export PLATE_MIN_SCORE=${PLATE_MIN_SCORE:-0.5}
export PLATE_SAMPLE_MS=${PLATE_SAMPLE_MS:-250}
export PLATE_VOTE_WINDOW=${PLATE_VOTE_WINDOW:-5}
export PLATE_VOTE_QUORUM=${PLATE_VOTE_QUORUM:-3}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -z "${PLATE_BACKEND_SCRIPT:-}" ] && [ -r "$script_dir/../backend/run_stream.sh" ]; then
    export PLATE_BACKEND_SCRIPT="$script_dir/../backend/run_stream.sh"
fi
exec sh "$script_dir/run.sh" "$@"
