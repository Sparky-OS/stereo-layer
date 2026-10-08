# stereo-layer

Shared stereo picture policy and presentation for Qt 6 applications on Sparky Stereo OS.

The library consumes `Stereo3DLayout` from the edition's Qt and KImageFormats readers. File marks precede input choices, format fallbacks and shape detection. Photo Sphere XMP vetoes shape detection. Unmarked JPS order is a format fallback, so an input choice can override it. MPO disparity records with unknown viewpoint order retain the two frames in stored order; an input order or Swap Eyes can override that fallback. MPF composite and multiple-angle records stay 2D. A bounded MPF index check distinguishes those records when the reader has no typed layout.

A picture's stereo surface contains full side-by-side views, left first. The window's controls stay on its ordinary surface. Widget and Quick adapters share the Wayland backend and renderer. A connection owns declaration state for all its surfaces; releasing one does not release another.

## Widget clients

Find `StereoLayer` with CMake and link `Stereo::Layer`. Call `Stereo::WidgetLayer::prepareWindow(topLevel)` before the native window is created. Create a `WidgetLayer` on the dedicated picture widget and call `setSource(path, options)`. The layer follows its rectangle, visibility, screen, zoom and pan. The source-loading path follows the screen's active output mode.

For viewers that already decode images, `Picture::fromImages` accepts raw frames, reader metadata, format and a bounded header. Pass the output mode from a `ScreenMode` watching this window's screen. `setPicture` presents that result. Set `setPaintArea(false)` when the adapter owns painting; call `paint` from that paint path to clear the hole or draw the ordinary image. Graphics-view adapters must also stop filling or brushing their viewport while the layer is active.

## Quick clients

Import `org.sparky.stereo 1.0` and use `StereoPicture`. Its properties are `source`, `swapEyes`, `inputLayout`, `inputOrder`, `anaglyph`, `view`, `zoom` and `pan`. Each item follows its own window's screen and active output mode. Local and qrc URLs are supported, with relative QML URLs resolved in their context.

The item's window needs an alpha buffer before native creation. The item prepares a window it joins before creation; a client creating its window earlier must call `PictureItem::prepareWindow` first. Unsupported rotation, ancestor scaling, opacity and clipping use the ordinary image rather than letting a subsurface escape the item's visible area. Put the stereo item in an untransformed, unclipped area, with normal Quick overlays above it.

The integer properties use the public C++ enum values: layout 0 automatic, 1 side by side, 2 top and bottom, 3 multiview, 4 rows, 5 columns; order 0 left first, 1 right first; anaglyph 0 automatic, 1 red/cyan, 2 green/magenta, 3 amber/blue; view 0 stereo, 1 left only, 2 right only, 3 original 2D. Checkerboard input is reserved and unsupported. Format descriptions are separate from display choices such as Swap Eyes and original 2D.

## Anaglyph colour

Colour is reconstructed and approximate: an anaglyph keeps only some of each eye's colour. The original file is never changed. Present `Picture::colorDisclaimer()` when offering conversion; Quick exposes `colorDisclaimer` and `approximateColor` for the interface to show the note.

On an active anaglyph output, the original image remains 2D. Conversion is used only for a true stereo output. Unknown and ordinary outputs also preserve the original. Mode visibility is not mode selection, and the library never selects a display mode. `ScreenMode` observes the active KScreen mode and tracks configuration changes independently for each screen, including paired outputs.

## Build and verification

CMake uses ECM, Qt 6, libstereo-declare and the edition's KScreen stereo API. `BUILD_TESTING=ON` enables picture/reader/geometry checks and a three-screen KScreen Fake test. `BUILD_EXAMPLES=ON` builds the widget/QML client. Local KDE CI runs clean GCC and clang builds in `photos-layer-build:2`, the edition image prepared for this workspace. This image is local; the pipeline is not claimed to run on an external registry.

Compositor evidence, package recipes for the review image and measurements are kept outside this repository in the partner workspace's REPORT.md and evidence/layer/. The backend currently targets Linux native Wayland and uses 8-bit RGB shared-memory buffers, matching the ported Gwenview path. X11, deep-colour presentation and other operating systems are not verified. Viewer migrations are subsequent milestones.

Sparky Stereo OS, an edition of SparkyLinux, created by Paweł "pavroo" Pijanowski <pavroo@onet.eu> and Daniel Ramos (Capitain Jack) <Capitain_Jack@yahoo.com>.

Code derived from Gwenview and the Marble/Krita window pattern retains GPL-2.0-or-later licensing and its original credits.
