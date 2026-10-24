# Content inspection tests

These tests exercise the metadata API and its private-session service.
They do not establish desktop integration or complete format coverage.
`fixtures.py` authors Matroska StereoMode 0–14 with FFmpeg and CRC-checked PNG sTER chunks with Python.
`codec-fixtures.py` adds x264-authored AVC marks, explicitly authored HEVC prefix SEI (including reserved-value negatives), and FFmpeg-muxed spherical bounds.
The HEVC writer uses H.265 D.2.16; the installed x265 need not provide a frame-packing option.
`spherical-fixture.c` enables FFmpeg's unofficial MP4 metadata writing explicitly, so the resulting file actually contains its requested marks.
`mp4-fixtures.py` walks sized ISO BMFF boxes, preserves the samples and tests Spherical Video V2 st3d values 0–4, both st3d/vexu orders, reserved values and duplicate rejection.
Expected results are explicit fixture data, independent of the detector's mapping table.

Run in an isolated build container with FFmpeg 9 development headers, zlib, GLib/GIO, Python, a C compiler, D-Bus and shared-mime-info:

```sh
sh autotests/content/build-standalone.sh /work/content-test
cc autotests/content/spherical-fixture.c $(pkg-config --cflags --libs libavformat libavcodec libavutil) -o /work/content-test/spherical-fixture
python3 autotests/content/fixtures.py /work/content-fixtures
python3 autotests/content/codec-fixtures.py /work/content-fixtures /work/content-test/spherical-fixture
python3 autotests/content/mp4-fixtures.py /work/content-fixtures
python3 autotests/content/check.py /work/content-fixtures --probe /work/content-test/stereo-content-probe
dbus-run-session -- python3 autotests/content/service-test.py /work/content-test /work/content-fixtures
python3 autotests/content/catalogue-test.py /work/content-test/content/stereo-content.xml
```

Without `--probe`, the checker queries the installed GIO engine for the original baseline.
It deliberately fails until the engine hooks and MIME catalogue are installed.
The metadata result distinguishes absent marks (`stereo=-1`) from explicit mono (`stereo=0`); both retain the ordinary type.
Custom stereo packing is `layout=-1`; it is never guessed to be native multiview.
Repeat the standalone build with `CC=clang` to check both compilers.

The MP4 cases require the demuxer to retain st3d custom/right-first modes and complementary vexu information.
An unpatched FFmpeg reports the missing metadata or rejects the reversed box order; these are retained failures, not skipped tests.
Use the owner's coordinated FFmpeg build for release proof.
A minimal local FFmpeg build is sufficient for parser diagnosis but is not a replacement distribution package.

Timings printed by the service test are information, not pass thresholds.
This test measures a new client process querying a warm shared cache; it does not measure player startup or window creation.
