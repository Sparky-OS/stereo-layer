# stereo-layer

Shared stereo picture policy and presentation for Qt 6 applications on Sparky Stereo OS.

The library consumes `Stereo3DLayout` from the edition's Qt and KImageFormats readers. File marks precede input choices, format fallbacks and shape detection. A picture without a mark is a side-by-side pair by its shape alone only from 2:1 (two squares) to 3.6:1 (two 16:9 pictures are 3.56:1); a wider one is a panorama, and the shape never says top and bottom, so portraits and phone screenshots stay one picture. Photo Sphere XMP vetoes shape detection. Unmarked JPS order is a format fallback, so an input choice can override it. MPO disparity records with unknown viewpoint order retain the two frames in stored order; an input order or Swap Eyes can override that fallback. MPF composite and multiple-angle records stay 2D. A bounded MPF index check distinguishes those records when the reader has no typed layout.

A picture's stereo surface contains full side-by-side views, left first. The window's controls stay on its ordinary surface. Widget and Quick adapters share the Wayland backend and renderer. A connection owns declaration state for all its surfaces; releasing one does not release another.

## Widget clients

Find `StereoLayer` with CMake and link `Stereo::Layer`. Call `Stereo::WidgetLayer::prepareWindow(topLevel)` before the native window is created. Create a `WidgetLayer` on the dedicated picture widget and call `setSource(path, options)`. The layer follows its rectangle, visibility, screen, zoom and pan. The source-loading path follows the screen's active output mode.

For viewers that already decode images, `Picture::fromImages` accepts raw frames, reader metadata, format and a bounded header. Pass the output mode from a `ScreenMode` watching this window's screen. `setPicture` presents that result. Set `setPaintArea(false)` when the adapter owns painting; call `paint` from that paint path to clear the hole or draw the ordinary image. Graphics-view adapters must also stop filling or brushing their viewport while the layer is active.

## Quick clients

Import `org.sparky.stereo 1.0` and use `StereoPicture`. Its properties are `source`, `swapEyes`, `inputLayout`, `inputOrder`, `anaglyph`, `view`, `zoom` and `pan`. Each item follows its own window's screen and active output mode. Local and qrc URLs are supported, with relative QML URLs resolved in their context.

The item's window needs an alpha buffer before native creation. The item prepares a window it joins before creation; a client creating its window earlier must call `PictureItem::prepareWindow` first. Unsupported rotation, ancestor scaling, opacity and clipping use the ordinary image rather than letting a subsurface escape the item's visible area. Put the stereo item in an untransformed, unclipped area, with normal Quick overlays above it.

The integer properties use the public C++ enum values: layout 0 automatic, 1 side by side, 2 top and bottom, 3 multiview, 4 rows, 5 columns; order 0 left first, 1 right first; anaglyph 0 automatic, 1 red/cyan, 2 green/magenta, 3 amber/blue; view 0 stereo, 1 left only, 2 right only, 3 original 2D. Checkerboard input is reserved and unsupported. Format descriptions are separate from display choices such as Swap Eyes and original 2D.

## Video

Players share one rule through `Stereo::Video::layout(displaySize, mark, options)` (header `video.h`): the stream's own mark first (Matroska's StereoMode, RFC 9559; the frame packing SEI of H.264 and H.265, Table D-8; the st3d box), in the words of the `Stereo3DLayout` text, then the user's choice, then the frame's shape. A frame without a mark is side by side by its shape alone only when it is at least three times as wide as high (`Video::SideBySideShape`): two pictures next to each other, as full side by side 16:9 (3.56:1) is, never one picture, as a 2.39:1 scope film is. Below that, and for top and bottom, the shape decides nothing and the user's choice covers the rest. `displaySize` is the frame as shown, with its sample aspect ratio applied. Programs without Qt, or that load the library at run time, call `stereo_layer_video_layout(width, height, mark, &layout, &order, &source)`, which returns the same enum values as integers. Still pictures keep `Picture`'s own rule.


Colour is reconstructed and approximate: an anaglyph keeps only some of each eye's colour. The original file is never changed. Present `Picture::colorDisclaimer()` when offering conversion; Quick exposes `colorDisclaimer` and `approximateColor` for the interface to show the note.

On an active anaglyph output, the original image remains 2D. Conversion is used only for a true stereo output. Unknown and ordinary outputs also preserve the original. Mode visibility is not mode selection, and the library never selects a display mode. `ScreenMode` observes the active KScreen mode and tracks configuration changes independently for each screen, including paired outputs.

## Build and verification

CMake uses ECM, Qt 6, libstereo-declare and KScreen's stereo API. libstereo-declare is in [our plasma-wayland-protocols branch](https://invent.kde.org/danielcamposramos/plasma-wayland-protocols/-/tree/stereo3d-6.7/stereo-declare) and the stereo API in [our libkscreen branch](https://invent.kde.org/danielcamposramos/libkscreen/-/tree/stereo3d-6.7). `BUILD_TESTING=ON` enables picture/reader/geometry checks, a three-screen KScreen Fake test and an unlinked QML consumer import test. `BUILD_EXAMPLES=ON` builds the widget/QML client. Clean GCC and clang builds pass all of them.

Headless KWin runs with both eyes captured check fit, zoom and pan, swap, hide, the selected 2D view and ordinary pictures: one declaration per picture, none for ordinary images, and nothing outside the picture differs between the eyes. The backend targets Linux native Wayland and uses 8-bit RGB shared-memory buffers, matching the ported Gwenview path. X11, deep-colour presentation and other operating systems are not verified. Viewer migrations are subsequent milestones.

Sparky Stereo OS, Daniel Ramos's (Capitain Jack) edition of SparkyLinux (https://sparkylinux.org, by Paweł "pavroo" Pijanowski).

Code derived from Gwenview and the Marble/Krita window pattern retains GPL-2.0-or-later licensing and its original credits.
