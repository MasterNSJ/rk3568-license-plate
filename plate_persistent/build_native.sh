#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sdk_dir=${RK3568_SDK_ROOT:-"$HOME/rk3568_linux_sdk"}
compiler=${RK3568_CXX:-/opt/atk-dlrk356x-toolchain/bin/aarch64-buildroot-linux-gnu-g++}
sysroot=$("$compiler" --print-sysroot)
api_dir="$sdk_dir/external/rknpu2/runtime/RK356X/Linux/librknn_api"
detector=${PLATE_DETECT_MODEL:-"$project_dir/../plate_detector/model/plate_detect.rknn"}
recognizer=${PLATE_REC_MODEL:-"$project_dir/../lprnet/model/lprnet.rknn"}
package_dir="$project_dir/install/plate_persistent"

for required in "$compiler" "$api_dir/include/rknn_api.h" "$api_dir/aarch64/librknnrt.so" \
    "$sysroot/usr/include/opencv4/opencv2/core.hpp" "$detector" "$recognizer" \
    "$project_dir/main.cpp" "$project_dir/plate_engine.cpp" "$project_dir/plate_engine.h" \
    "$project_dir/plate_algorithms.h" "$project_dir/plate_quality.h" "$project_dir/run_image.sh" \
    "$project_dir/stream_protocol.h" "$project_dir/run_stream.sh"; do
    if [ ! -f "$required" ]; then
        printf 'Missing required file: %s\n' "$required" >&2
        exit 1
    fi
done

mkdir -p "$package_dir/lib" "$package_dir/model" "$package_dir/images"
"$compiler" -std=c++17 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic \
    -I "$api_dir/include" -I "$sysroot/usr/include/opencv4" \
    "$project_dir/main.cpp" "$project_dir/plate_engine.cpp" \
    -L "$api_dir/aarch64" -L "$sysroot/usr/lib" \
    "-Wl,-rpath-link,$sysroot/usr/lib" '-Wl,-rpath,$ORIGIN/lib' \
    -lrknnrt -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -pthread -ldl \
    -o "$package_dir/plate_native"

cp "$api_dir/aarch64/librknnrt.so" "$package_dir/lib/librknnrt.so"
cp "$detector" "$package_dir/model/plate_detect.rknn"
cp "$recognizer" "$package_dir/model/lprnet.rknn"
cp "$project_dir/run_image.sh" "$package_dir/run_image.sh"
cp "$project_dir/run_stream.sh" "$package_dir/run_stream.sh"
chmod +x "$package_dir/run_stream.sh"
chmod +x "$package_dir/plate_native" "$package_dir/run_image.sh"
/usr/bin/file "$package_dir/plate_native"
sha256sum "$package_dir/model/plate_detect.rknn" "$package_dir/model/lprnet.rknn"
printf '\nPackage ready: %s\n' "$package_dir"
printf 'OpenCV is linked from the matching target sysroot (4.5.5), not the SDK static 3.4.5 bundle.\n'

