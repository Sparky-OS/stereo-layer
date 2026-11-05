# The default way of drawing the views from a picture and its depth

A picture with its depth map is assembled into full side by side by libstereo-dibr, the left eye first, each eye at the picture's size.
Where the assembly has options, the library carries the best default and the person's choice (`struct stereo_dibr_options`, `stereo_dibr_options_default()`), and every player shows the same controls (`Stereo::DepthControls` for Qt widgets, `DepthControls` in QML).

"Best" is measured, not guessed.
`autotests/dibrbench.c` draws scenes whose true views are known: textured shapes in layers in front of a sloped textured background, each eye's image evaluated analytically, so the two true views and the exact depth map of the picture are known.
Each way of drawing is scored against the true views: the mean absolute error over both eyes (`all`, 0 to 255) and where the picture shows nothing of the place (`holes`, the places a plain forward warp leaves empty).
Eight scenes: three exact at the default depth budget, two at other budgets, three with depth noise.
The test fails when the library's default is not the best of the ways measured.

| Way (picture as the left eye) | all | holes | per frame, 960x540 on one core |
|---|---|---|---|
| forward warp, background fill (the earlier default) | 0.806 | 5.112 | 31 ms |
| forward, depth-aided fill | 0.748 | 4.232 | 146 ms |
| forward, pyramid fill | 0.743 | 4.376 | 55 ms |
| forward, Telea inpainting | 0.769 | 4.486 | 410 ms |
| backward, depth-aided | 0.745 | 4.232 | 159 ms |
| backward, pyramid | 0.740 | 4.374 | 55 ms |
| layered, background fill | 0.789 | 4.984 | 52 ms |
| **layered, depth-aided fill (the default)** | **0.736** | **4.137** | 106 ms |
| layered, pyramid fill | 0.746 | 4.348 | 73 ms |
| layered, Telea inpainting | 0.748 | 4.290 | 239 ms |
| forward, background, smoothing the depth | 1.495 | 6.296 | 91 ms |
| forward, holes left black (control) | 8.753 | 69.919 | 31 ms |

- Smoothing the depth is worse when the depth is exact and no better when it is noisy, so it is off.
- The picture as the left eye keeps that eye exact; a picture that is really the middle one, taken for the left eye, costs 12.3 instead of 0.7: which eye the picture is belongs to where the picture came from, so it is an option the person can set, and the left eye is the default.
- Strength is the picture's parallax budget, in pixels of a picture 1920 wide: 58 pixels is 1.03 degrees of visual angle for a viewer three picture heights from the screen, and the area the picture does not show grows with it (1.2 % of the right eye at 20 pixels, 3.8 % at 58, 8.0 % at 120).
- The cost is time: layered warping with depth-aided filling takes about three times as long as the earlier default on the processor, and eleven times on a GPU; every way remains a choice.

## The default for a video: the best way that keeps up

A still picture, and the saving of a picture into a file, have no time limit: they use the default above, the best way measured. A video must keep its frame rate, and the best way does not on the GPUs measured at 1920x1080 per eye (dibrtime, the passes alone, the 95th percentile of twelve frames):

| Way | RTX 3060 | Ryzen 5600G Vega | Radeon R5 (A10-9600P) |
|---|---|---|---|
| layered, depth-aided (the best) | 72 ms | 437 ms | 2583 ms |
| layered, pyramid | 15 ms | | |
| forward, pyramid | 6.6 ms | | |
| forward, background (the least costly) | 4.7 ms | 30 ms | 146 ms |

(The cells of the other GPUs are in the report of the order: ORDER 411, group 2e.)
The library therefore carries a ladder of four ways that no other way beats at its cost (`stereo_dibr_ways()`: layered with depth-aided, layered with pyramid, forward with pyramid, forward with background; the bench fails when the ranking of the ladder is not the measured one), and chooses from it for each video: the first way whose slowest frames (the 95th percentile) fit four fifths of the frame's time, measured on the GPU that draws, at the size drawn, once (`stereo_dibr_gl_measure()`, `stereo_dibr_measure_cpu()`; the numbers are kept in `XDG_CACHE_HOME/stereo-dibr/timings`, never in a media file, and the measuring takes about a second on a GPU that draws a way in ten milliseconds and about ten seconds on one that takes a second). Where no way fits, the quickest is chosen and the program says so. The person's own choice of a warping or a gap filling (`way=own`) wins and holds for everything, the slowest way included.
