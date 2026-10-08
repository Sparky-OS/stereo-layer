# stereo-layer

Shared stereo picture policy and presentation for Qt 6 applications on Sparky Stereo OS.

File marks precede input choices, format fallbacks and shape detection. Ordinary 2D interfaces remain on the parent surface. A stereo picture uses a separate full side-by-side, left-first surface below it. The library depends on libstereo-declare.

Anaglyph colour reconstruction is approximate because an anaglyph keeps only some of each eye's colour. The original file is never changed. On a screen using anaglyph output, the original image remains 2D; conversion is used only for true stereo outputs. Screen mode is never selected by this library.

This repository is being built as goal 2 step 2. Widget and QML integration are not yet verified.

Sparky Stereo OS, an edition of SparkyLinux, created by Paweł "pavroo" Pijanowski <pavroo@onet.eu> and Daniel Ramos (Capitain Jack) <Capitain_Jack@yahoo.com>.

Code derived from Gwenview, Marble and Krita retains GPL-2.0-or-later licensing and its original credits.
