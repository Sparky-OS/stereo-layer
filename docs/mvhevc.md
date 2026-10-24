# MV-HEVC decoding and presentation

The optional runtime library libstereo-av shares the FFmpeg multiview assembly used by mpv, Qt Multimedia and VLC Stereo.
It depends on FFmpeg, and does not link Qt or change the existing layer ABI.
Its source is LGPL-2.1-or-later.

Enable FFmpeg's HEVC view_ids option before opening the decoder.
Call stereo_av_pair_configure from get_format, where the active VPS view IDs are guaranteed to be accurate.
A hardware decoder that exposes only the base view must yield to the software multiview route.
The existing automatic hardware route for ordinary videos is retained.

The decoder's AV_FRAME_DATA_VIEW_ID and AVStereo3D left/right positions identify the two images.
A base layer is not assumed to be the left eye.
Pair only distinct signalled views with equal known timestamps and compatible image descriptions.
The pending state holds at most one image, and must be reset on decoder flush, seek, source replacement and sequence replacement.
An incomplete pair is never completed by duplicating the base view.

Copy the two decoded pictures into one left-first SBS image without resizing.
Retain each sample's precision, range and colour description.
The resulting AVStereo3D side data says SIDEBYSIDE and PACKED, with the MOV vexu presentation fields preserved.
The frame's sample aspect ratio remains that of one view.
Consumers expose the original dimensions of one view to their existing stereo presentation policy.
Only their separate video surface is declared to Stereo KWin, which performs the output resize.

When stereo is disabled or the eye identities are unavailable, select the VPS base view and mark it as ordinary 2D.
The per-file eye correction remains in each player's existing state store and is applied after assembly.
This reader path never writes media bytes or file attributes.
The generated fixture is CC0, encoded with x265's multiview implementation and muxed with explicit lhvC and vexu boxes.
The tests compare every plane of 96 pairs with independent decoded views and exercise reversed arrival, duplicates, timestamp disagreement, format disagreement and reset.
