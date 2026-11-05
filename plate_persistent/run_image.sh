#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
if [ "$#" -lt 1 ]; then
    printf 'Usage: sh run_image.sh /path/to/photo.jpg [output_directory]\n' >&2
    exit 2
fi
image=$1
output=${2:-"$app_dir/result_$(date +%Y%m%d_%H%M%S)_$$"}
exec "$app_dir/plate_native" "$app_dir/model/plate_detect.rknn" \
    "$app_dir/model/lprnet.rknn" "$image" "$output"
