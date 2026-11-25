# JXLStereo and animated pair fixtures

`generate.cpp` writes the JPEG XL pictures and `animated-sequence.avif` with libjxl's and libavif's encoders; `avif-stereo-track.py` adds the ISOBMFF stereo video scheme to that sequence as `animated-stvi.avif`.
The commands are at the top of `generate.cpp`.
The left view of every picture is red and the right view blue; the green sample is 1000 times the frame number plus 1000.
The two SVG drawings move one marker in each view on one timeline; one carries the SVGStereo mark and the other does not.
