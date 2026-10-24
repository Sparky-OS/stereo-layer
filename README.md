# stereo-layer

Shared stereo picture policy and presentation for Qt 6 applications on Sparky Stereo OS.

The library consumes `Stereo3DLayout` from the edition's Qt and KImageFormats readers, and reads the whole set exactly: `mono`; `sideBySide`, `sideBySideQuincunx`, `topBottom`, `checkerboard`, `rowInterleaved`, `columnInterleaved`, `frameSequential`, `blockLaced` and `multiView`, each `LeftFirst` or `RightFirst`; `anaglyphCyanRed` and `anaglyphGreenMagenta` (the red and the green view are the left eye's); and JPS's `anaglyphRedLeft` and `anaglyphRedRight` (RFC 9559 StereoMode, section 5.1.4.1.28.3; H.264 and H.265 Table D-8 (H.264 V15, 08/2024; D-9 in H.264 V16, 06/2026)). A value outside the set means one picture. Interleaved, checkerboard and quincunx views are upconverted to the frame's size, each missing sample the mean of its two neighbours, as a display does; a still never holds frame-sequential or block-laced views. File marks precede input choices, format fallbacks and shape detection. A picture without a mark is a side-by-side pair by its shape alone only from 2:1 (two squares) to 3.6:1 (two 16:9 pictures are 3.56:1); a wider one is a panorama, and the shape never says top and bottom, so portraits and phone screenshots stay one picture. Photo Sphere XMP vetoes shape detection. Unmarked JPS order is a format fallback, so an input choice can override it. MPO disparity records with unknown viewpoint order retain the two frames in stored order; an input order or Swap Eyes can override that fallback. MPF composite and multiple-angle records stay 2D. A bounded MPF index check distinguishes those records when the reader has no typed layout.

A picture's stereo surface contains full side-by-side views, left first. The window's controls stay on its ordinary surface. Widget and Quick adapters share the Wayland backend and renderer. A connection owns declaration state for all its surfaces; releasing one does not release another.

## Stereo SVG files

SVG has no stereo mark of its own, so the policy reads a proposed one, SVGStereo: an attribute `layout` in the namespace `https://github.com/Sparky-OS/svgstereo` on the root `svg` element, with the `Stereo3DLayout` values (`sideBySideLeftFirst`, `sideBySideRightFirst`, `topBottomLeftFirst`, `mono` and the rest), the viewBox holding both views:

```xml
<svg xmlns="http://www.w3.org/2000/svg" xmlns:svgstereo="https://github.com/Sparky-OS/svgstereo"
     svgstereo:layout="sideBySideLeftFirst" viewBox="0 0 200 100">
```

SVG renderers ignore attributes from other namespaces (SVG 1.1 Second Edition, section 23.1, "Foreign namespaces and private data"), so a marked file draws as before everywhere else. A compressed SVG (`.svgz`, a gzip stream, RFC 1952; SVG 1.1 Second Edition, section 1.2) carries the mark the same way and is read the same way. An SVG without the mark is a drawing, one picture: neither its colours nor its shape make it a pair. The namespace and the attribute name are a proposal until they are published.

## Programs without Qt

`libstereo-policy` (header `stereo-policy.h`, pkg-config `stereo-policy`) is the same policy as a small C library, for a browser or a player: `stereo_policy_read_mark()` reads a Stereo3DLayout value exactly; `stereo_policy_video_layout()` gives a video frame's packing from the user's explicit choice (a packing, or `STEREO_POLICY_USER_FORCE_2D`), then the stream's mark, then the shape (three times as wide as high), and applies the user's swap-eyes correction over whatever decided; `stereo_policy_video_full_views()` says whether the views are full or half (a marked side by side frame wider than a single picture can be, 12:5, is full, a top and bottom frame narrower than 5:4 is full, otherwise half; interleaved, checkerboard and quincunx views are half); the shape rules are there too. The values are those of the Qt API's enumerations.

The user's correction for a file whose mark has its eyes the wrong way round is stored with the file, as the extended attribute `user.stereo-policy.swap-eyes` (`stereo_policy_file_swap_eyes()`, `stereo_policy_set_file_swap_eyes()`; `Picture::userSwapEyes()` and `Picture::setUserSwapEyes()` in Qt), so it survives a restart and follows the file through copies and moves that keep extended attributes. `Picture::load()` and `Picture::loadPair()` apply it over the file's mark, on top of the program's own swap; a thumbnail made before the correction is the program's to make again (Dolphin removes it from the thumbnail cache). libstereo-layer builds on it, so every program decides alike; its own C entries `stereo_layer_video_layout()` and `stereo_layer_video_full_views()` stay.
This store is being changed: the edition's rule is that a program never writes to the user's file on its own, so the correction moves into each program's own per-file state, next to what it already remembers per file (a player's resume position), and is saved to the file only when the person chooses that, after which the file carries it and the per-file option is unmarked again; the definitive eye swap stays with the compositor's output.

## Depth to stereo

**IANA registration, 20 October 2026:** [Matroska Track Plane Types](https://www.iana.org/assignments/matroska#matroska-track-plane-types) now assigns **3, 4 and 5** to the depth maps of the left-eye, right-eye and background planes, respectively, with Daniel Ramos as change controller and the [stereo-layer v1.9.0 specification](https://github.com/Sparky-OS/stereo-layer/blob/v1.9.0/docs/depth-marks.md) as reference.
This gives separate picture and depth tracks shared identifiers through `TrackCombinePlanes`, so readers can preserve their relationship before rendering stereo views.
The separate request for six packed-frame `StereoMode` values (proposed 15–20) remains under designated-expert review; those values are not assigned.

A picture and its depth map are a stereo format too. [docs/depth-marks.md](docs/depth-marks.md) defines the marks: the depth layouts of `Stereo3DLayout` (the picture and its depth side by side or top and bottom either way, HDMI 1.4a's four rows with graphics, four quarters with the background, or the depth map on its own) and `Stereo3DDepth`, what a depth sample means, with the format each value comes from.

`libstereo-dibr` (header `stereo-dibr.h`, pkg-config `stereo-dibr`) is depth-image-based rendering as a small C library: a picture and its depth map become the two views of full side by side, each at the picture's size, left eye first, so every program that turns depth into stereo draws the same views. `stereo_depth_read_mark()` reads `Stereo3DDepth`; `stereo_depth_parallax_table()` gives each sample value its parallax in pixels of the view, positive behind the screen: 2D-plus-depth (larger samples nearer, or farther), the four depth representation types of H.265 Table G.3 and H.264 Table H-1 (DepthLUT of equation G-10 from `stereo_depth_nonlinear_lut()`), Dynamic Depth's RangeLinear and RangeInverse, a sensor's distances with 0 for no measurement, and the parallax map of ISO/IEC 23002-3 as we read that paid standard (unverified). `stereo_dibr_render()` draws the views, every path a choice: the picture as the left eye (default) or the middle; forward warping (default), backward warping, or layered warping with a boundary layer over the foreground's edges; holes filled by background extension (default), depth-aided filling, Telea's inpainting from the background side, or left at a given value; the depth smoothed by an asymmetric Gaussian or not (default). `stereo_dibr_render_layer()` adds a second layer, the background the picture hides or graphics over it. `stereo_dibr_unpack()` finds the picture, the depth map and the layer of a frame packed in any depth layout, as views of its planes, for players that draw a decoded frame. `stereo-dibr-gl.h` runs the same steps as GLSL passes in the caller's OpenGL 3.3 or OpenGL ES 3.0 context, with no GL library of its own; the shaders are installed in `share/stereo-dibr/glsl`. `Picture::fromDepth()` and the depth layouts of `Picture::Layout` draw marked pictures for Qt viewers, and `Picture::load()` and `Picture::loadPair()` read them from any image reader that gives the depth map as its second image (the edition's KImageFormats does for HEIF and AVIF depth items, Dynamic Depth, GDepth and Apple's portrait photos), with the user's drawing options and the choice of the picture the views are drawn from, the one the file shows or the original before the camera's effects. FFmpeg's `dibr` filter is built on the library.

## Widget clients

Find `StereoLayer` with CMake and link `Stereo::Layer`. Call `Stereo::WidgetLayer::prepareWindow(topLevel)` before the native window is created; it gives the window an alpha buffer and keeps its opaque background, while a tooltip or a popup keeps the panel its style draws (rounded corners, a shadow). Create a `WidgetLayer` on the dedicated picture widget and call `setSource(path, options)`. The layer follows its rectangle, visibility, screen, zoom and pan. The source-loading path follows the screen's active output mode. What the layer hands the compositor is the part of each eye that shows, copied at the resolution the eye has and never scaled (`cropEyes()` in `render.h`): a full side by side photo of two 6000x4000 views goes as 12000x4000 samples at most (beyond 16384 samples, a compositor's largest texture, the eyes are drawn at the area's size), a row-interleaved 1920x1080 picture as 3840x540, and Stereo KWin resizes them once to the picture's place in the area; the window draws the black around the picture.

For viewers that already decode images, `Picture::fromImages` accepts raw frames, reader metadata, format and a bounded header. Pass the output mode from a `ScreenMode` watching this window's screen. `setPicture` presents that result. Set `setPaintArea(false)` when the adapter owns painting; call `paint` from that paint path to clear the hole or draw the ordinary image. An adapter that gives each eye a whole view of its area, clear outside its stereo pictures, sets `setKeepTransparency(true)`: the clear parts then show what is behind the window, as at its rounded corners, instead of black. Graphics-view adapters must also stop filling or brushing their viewport while the layer is active.

## Thumbnails

Programs that show pictures small call `Picture::loadPair(path, bound, options)`: the same policy as `load()` on an output that is not stereo, with each image decoded to fit within the bound where the reader can, and the views scaled so that both side by side fit it. A file whose marks, format and shape cannot make a pair is not decoded and gives an empty picture, so a thumbnailer keeps its own path for ordinary pictures. `Picture::sideBySide()` returns both views side by side, left first, with `Stereo3DLayout` set to `sideBySideLeftFirst` in the image's text: the form a thumbnail of a stereo file takes.

## Quick clients

Import `org.sparky.stereo 1.0` and use `StereoPicture`. Its properties are `source`, `swapEyes`, `inputLayout`, `inputOrder`, `anaglyph`, `view`, `zoom` and `pan`. Each item follows its own window's screen and active output mode. Local and qrc URLs are supported, with relative QML URLs resolved in their context. On a screen that shows one eye (a 2D output) a pair is drawn as its left view and declares nothing; only a stereo or anaglyph output takes the pair.

The item needs a window of the windowing system to itself. A `QQuickWidget` has none, so a program that embeds QML in a widget window runs it in a `QQuickView` inside `QWidget::createWindowContainer()` (Calamares does so for its slideshow when the branding sets `slideshowWindow`).

The item's window needs an alpha buffer before native creation. The item prepares a window it joins before creation; a client creating its window earlier must call `PictureItem::prepareWindow` first. Unsupported rotation, ancestor scaling, opacity and clipping use the ordinary image rather than letting a subsurface escape the item's visible area. Put the stereo item in an untransformed, unclipped area, with normal Quick overlays above it.

The integer properties use the public C++ enum values: layout 0 automatic, 1 side by side, 2 top and bottom, 3 multiview, 4 rows, 5 columns; order 0 left first, 1 right first; anaglyph 0 automatic, 1 red/cyan, 2 green/magenta, 3 amber/blue; view 0 stereo, 1 left only, 2 right only, 3 original 2D. Checkerboard input is reserved and unsupported. Format descriptions are separate from display choices such as Swap Eyes and original 2D.

## Video

Players share one rule through `Stereo::Video::layout(displaySize, mark, options)` (header `video.h`): the user's explicit choice first (a packing, or not 3D through the overload with `forceMono`), because a mark can be wrong; then the stream's own mark (Matroska's StereoMode, RFC 9559; the frame packing SEI of H.264 and H.265, Table D-8 (H.264 V15, 08/2024; D-9 in H.264 V16, 06/2026); the st3d box), in the words of the `Stereo3DLayout` text; then the frame's shape. A frame without a mark is side by side by its shape alone only when it is at least three times as wide as high (`Video::SideBySideShape`): two pictures next to each other, as full side by side 16:9 (3.56:1) is, never one picture, as a 2.39:1 scope film is. Below that, and for top and bottom, the shape decides nothing and the user's choice covers the rest. `displaySize` is the frame as shown, with its sample aspect ratio applied. Programs without Qt, or that load the library at run time, call `stereo_layer_video_layout(width, height, mark, &layout, &order, &source)`, which returns the same enum values as integers. Still pictures keep `Picture`'s own rule.


Colour is reconstructed and approximate: an anaglyph keeps only some of each eye's colour. The original file is never changed. Present `Picture::colorDisclaimer()` when offering conversion; Quick exposes `colorDisclaimer` and `approximateColor` for the interface to show the note.

On an active anaglyph output, the original image remains 2D. Conversion is used only for a true stereo output. Unknown and ordinary outputs also preserve the original. Mode visibility is not mode selection, and the library never selects a display mode. `ScreenMode` observes the active KScreen mode and tracks configuration changes independently for each screen, including paired outputs.

## Build and verification

CMake uses ECM, Qt 6, libstereo-declare and KScreen's stereo API. libstereo-declare is in [our plasma-wayland-protocols branch](https://invent.kde.org/danielcamposramos/plasma-wayland-protocols/-/tree/stereo3d-6.7/stereo-declare) and the stereo API in [our libkscreen branch](https://invent.kde.org/danielcamposramos/libkscreen/-/tree/stereo3d-6.7). `BUILD_TESTING=ON` enables picture/reader/geometry checks, a three-screen KScreen Fake test and an unlinked QML consumer import test. `BUILD_EXAMPLES=ON` builds the widget/QML client. Clean GCC and clang builds pass all of them.

Headless KWin runs with both eyes captured check fit, zoom and pan, swap, hide, the selected 2D view and ordinary pictures: one declaration per picture, none for ordinary images, and nothing outside the picture differs between the eyes. The backend targets Linux native Wayland and uses 8-bit RGB shared-memory buffers, matching the ported Gwenview path. X11, deep-colour presentation and other operating systems are not verified. Viewer migrations are subsequent milestones.

Sparky Stereo OS, Daniel Ramos's (Capitain Jack) edition of SparkyLinux (https://sparkylinux.org, by Paweł "pavroo" Pijanowski).

Code derived from Gwenview and the Marble/Krita window pattern retains GPL-2.0-or-later licensing and its original credits.

### The 3D tag of a file name

`stereo_policy_name_tag()` (and `Stereo::Video::nameTag()`) reads the tag of a file name, for video that carries no mark of its own.
The name is cut into words at `.`, `_`, `-`, space, `[`, `]`, `(` and `)`; a word counts only whole and in any case, so "3DMark" and "Blender3D" say nothing.
The vocabulary: `3D` (the plain tag of 3D releases: says only that the video is 3D); `SBS`, `TAB`, `TB`, `OU` (side by side; top and bottom, as in the Blu-ray 3D scene's TAB and the over and under OU); the same with half views (`HSBS`, `H-SBS`, `HALF-SBS`, `HTAB`, `HOU`, `HTB` and so on) or full views (`FSBS`, `FULL-SBS`, `FTAB`, `FOU`, `FTB` and so on); `LR` and `RL` for the eye order (the Bino 1.x and 3dtv.at names; alone they mean side by side).
A word with half or full says both; a bare packing word leaves half or full to `stereo_policy_video_views()`, where a single picture's shape is half and twice that is full.
A bare `3D` gives side by side when the shape rule says so, else what the caller's comparison of the picture's halves says, else the video stays 2D and the user's menu covers it.
`Video::matroskaPlane()` (and `stereo_policy_matroska_plane()`) gives back the plane size that FFmpeg's Matroska demuxer folded into the sample aspect ratio, for `Video::views()`.
`stereo_policy_video_layout_named()` puts the tag after the user's explicit choice and the stream's mark and before the shape: a tag never overrides a mark.
Test vectors: `autotests/vectors/name-tags.tsv`.

`stereo_policy_halves_test()` is the call for a bare `3D` tag when a player can hand over one decoded frame (its brightness plane): it compares the halves left and right against above and below, allowing a small shift, and returns `STEREO_POLICY_HALVES_*` for `stereo_policy_video_layout_named()`; a flat or ordinary picture gives `UNKNOWN` and the video stays 2D.
A Matroska half pair whose DisplayWidth equals its pixel width reads through FFmpeg's doubled sample ratio as full, and each view still shows at its true display shape, so the picture is right even when the label says full (vector in `autotests/vectors/video-views.tsv`).


## Native X11 child placement

The proposed [support-version-8 child placement contract](docs/x11-child-place.md) registers `_KDE_NET_WM_STEREO_CONTENT_CHILD_PLACE`.
It gives a listed child's source-sized buffer an independent destination rectangle and optional placement below the ordinary parent surface.
The existing full-SBS declaration still describes the video; ordinary overlays declare no stereo layout.
