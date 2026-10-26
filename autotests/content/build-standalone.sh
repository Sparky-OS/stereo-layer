#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${1:?build directory required}
mkdir -p "$build"
python3 "$src/tools/content-mime.py" "$src/data/content/catalogue.json" "$build/content"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -I"$src/src/av" -I"$build/content" "$src/src/av/stereo-av.c" "$src/src/av/stereo-content.c" "$src/src/content/worker.c" $(pkg-config --cflags --libs libavcodec libavformat libavutil) -lz -o "$build/stereo-content-worker"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -I"$src/src/av" -I"$build/content" "-DSTEREO_CONTENT_WORKER=\"$build/stereo-content-worker\"" "$src/src/content/service.c" $(pkg-config --cflags --libs gio-unix-2.0) -o "$build/stereo-contentd"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -I"$src/src/content" "$src/src/content/client.c" "$src/autotests/content/client-test.c" $(pkg-config --cflags --libs gio-unix-2.0) -o "$build/client-test"

${CC:-cc} -std=c11 -Wall -Wextra -Werror -g -I"$src/src/av" -I"$build/content" "$src/src/av/stereo-av.c" "$src/src/av/stereo-content.c" "$src/src/content/probe.c" $(pkg-config --cflags --libs libavcodec libavformat libavutil) -lz -o "$build/stereo-content-probe"
