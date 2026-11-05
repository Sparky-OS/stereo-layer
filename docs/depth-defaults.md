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
| forward warp, background fill (the earlier default) | 0.806 | 5.112 | 30 ms |
| forward, depth-aided fill | 0.748 | 4.232 | 137 ms |
| forward, Telea inpainting | 0.769 | 4.486 | 369 ms |
| backward, depth-aided | 0.745 | 4.232 | 144 ms |
| layered, background fill | 0.789 | 4.984 | 52 ms |
| **layered, depth-aided fill (the default)** | **0.736** | **4.137** | 101 ms |
| layered, Telea inpainting | 0.748 | 4.290 | 213 ms |
| forward, background, smoothing the depth | 1.495 | 6.296 | 89 ms |
| forward, holes left black (control) | 8.753 | 69.919 | 32 ms |

- Smoothing the depth is worse when the depth is exact and no better when it is noisy, so it is off.
- The picture as the left eye keeps that eye exact; a picture that is really the middle one, taken for the left eye, costs 12.3 instead of 0.7: which eye the picture is belongs to where the picture came from, so it is an option the person can set, and the left eye is the default.
- Strength is the picture's parallax budget, in pixels of a picture 1920 wide: 58 pixels is 1.03 degrees of visual angle for a viewer three picture heights from the screen, and the area the picture does not show grows with it (1.2 % of the right eye at 20 pixels, 3.8 % at 58, 8.0 % at 120).
- The cost is time: layered warping with depth-aided filling takes about three times as long as the earlier default; every way remains a choice.
