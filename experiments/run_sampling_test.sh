#!/bin/sh
# Run on the RK3568, from the normal desktop session, with no other plate_ui open.
# Uses the installed upgraded binary. It starts only its own process and waits for it.
set -eu
duration=${PLATE_TEST_SECONDS:-60}
output=${1:-/userdata/plate_weekly/sampling}
ui_dir=${PLATE_UI_ROOT:-/userdata/plate_ui}
mkdir -p "$output"
for interval in ${PLATE_SAMPLE_INTERVALS:-1000 500 250}; do
    export PLATE_SAMPLE_MS="$interval" PLATE_AUTO_START=1 PLATE_RUN_SECONDS="$duration"
    export PLATE_METRICS_FILE="$output/metrics_${interval}.jsonl"
    # Each metrics file is a new run; refuse to mix with old evidence.
    if [ -e "$PLATE_METRICS_FILE" ]; then
        printf 'Output exists; choose a new result directory: %s\n' "$output" >&2
        exit 1
    fi
    (cd "$ui_dir" && exec sh run.sh) >"$output/ui_${interval}.log" 2>&1 &
    task_pid=$!
    printf 'observed_pid=%s\n' "$task_pid" >"$output/resources_${interval}.txt"
    trap 'kill "$task_pid" 2>/dev/null || true' INT TERM EXIT
    while kill -0 "$task_pid" 2>/dev/null; do
        if [ -r "/proc/$task_pid/status" ]; then
            { date +%s; grep -E 'VmRSS|VmSize|Threads' "/proc/$task_pid/status"; } >>"$output/resources_${interval}.txt"
        fi
        sleep 1
    done
    wait "$task_pid"
    trap - INT TERM EXIT
done
printf 'Sampling runs finished: %s\n' "$output"
