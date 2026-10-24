#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
build=${1:?build directory required}
mkdir -p "$build"
cc -std=c11 -Wall -Wextra -Werror -g -I"$src/src/av" "$src/src/av/stereo-av.c" "$src/src/av/stereo-content.c" "$src/src/content/worker.c" $(pkg-config --cflags --libs libavcodec libavformat libavutil) -lz -o "$build/stereo-content-worker"
cc -std=c11 -Wall -Wextra -Werror -g -I"$src/src/av" "-DSTEREO_CONTENT_WORKER=\"$build/stereo-content-worker\"" "$src/src/content/service.c" $(pkg-config --cflags --libs gio-unix-2.0) -o "$build/stereo-contentd"
cc -std=c11 -Wall -Wextra -Werror -g -I"$src/src/content" "$src/src/content/client.c" "$src/autotests/content/client-test.c" $(pkg-config --cflags --libs gio-unix-2.0) -o "$build/client-test"
