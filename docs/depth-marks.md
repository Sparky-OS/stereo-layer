# Stereo depth marks

A picture and its depth map are a stereo format.
A program that understands them draws the two views from the picture and the depth (depth-image-based rendering), and every other program shows the picture exactly as before.
These marks say that a file or a stream holds a picture with its depth, how the two are packed, and what a depth sample means.

Status: proposal, version 0.2, 17 October 2026 (0.1 the same day; 0.2 adds the readers' images and the phones' formats).
Editor: Daniel Ramos, for [Sparky Stereo OS](https://github.com/Sparky-OS/sparky-stereo-os).
Comments and objections are welcome in this repository's issues.

There are two marks.
`Stereo3DLayout`, the mark the edition already uses for every stereo picture and video, gains the depth layouts: where the picture and its depth are.
`Stereo3DDepth` is new: what a depth sample means.

## Stereo3DLayout: the depth layouts

The values sit beside the stereo layouts of [SVGStereo](https://github.com/Sparky-OS/svgstereo) and of the Stereo3DLayout text that the edition's readers report.
`PictureFirst` and `DepthFirst` say which comes first in the frame (the left half, the top half), as `LeftFirst` and `RightFirst` do for two views.

| `Stereo3DLayout` | The frame holds | Where it comes from |
|---|---|---|
| `depthSideBySidePictureFirst` | the picture in the left half, its depth map in the right half | 2D-plus-depth as Philips WOWvx displays took it |
| `depthSideBySideDepthFirst` | the depth map in the left half, the picture in the right half | the same, the other way round |
| `depthTopBottomPictureFirst` | the picture in the top half, its depth map in the bottom half | HDMI 1.4a, 3D_Structure 0100 "L + depth" (Appendix H, Figure H-5), the active space taken out |
| `depthTopBottomDepthFirst` | the depth map in the top half, the picture in the bottom half | the same, the other way round |
| `depthGraphicsTopBottom` | four rows from the top: the picture, its depth map, graphics, the graphics' depth map | HDMI 1.4a, 3D_Structure 0101 "L + depth + graphics + graphics-depth" (Figure H-6) |
| `depthBackgroundQuarters` | four quarters: the picture (top left), its depth map (top right), the background it hides (bottom left), the background's depth map (bottom right) | 2D-plus-depth with occlusion data as Philips "Declipse" took it |
| `depthSeparate` | the picture alone: its depth map is an image or a track of its own | an HEIF depth item, an MPEG-C Part 3 auxiliary video stream, an H.265 depth layer, a Dynamic Depth container item, a Matroska depth plane |

The depth map of a packed frame holds gray: a reader takes the first plane of a YUV frame (Y) and the gray level of an RGB one.
Its samples are read through `Stereo3DDepth`.

A picture reader that gives a `depthSeparate` picture's depth map as an image of its own gives it as its second image (`QImageReader::jumpToImage(1)`), one gray channel, 8 or 16 bits (samples of other depths spread over 16 bits).
When the file also keeps the picture before the camera's effects (Dynamic Depth's Image with `ItemSemantic` Original, GDepth's `GImage`), the reader gives it as its third image.
The first image is the picture exactly as the file shows it.

Philips' white paper "3D Interface Specifications" is not available to us: the positions of the WOWvx and Declipse planes come from second-hand descriptions and are unverified.
The marks themselves are exact: a file marked `depthBackgroundQuarters` has its planes where the table says.

For `depthGraphicsTopBottom`, a depth sample of 0 in the graphics' depth map means no graphics there: HDMI 1.4a does not say how the graphics plane is keyed, and this is our rule.
For `depthBackgroundQuarters`, a background depth sample of 0 means nothing there.

## Stereo3DDepth: what a sample means

The value is a word for the kind, then `key=value` pairs, separated by spaces.
Numbers are written in the C notation (a point, never a comma, whatever the language), with an optional exponent.
Unknown keys are skipped, so later versions can add some; an unknown kind means one picture.

```
Stereo3DDepth = "inverseZ near=0.25 far=10"
Stereo3DDepth = "nearness zero=0.5 range=58"
Stereo3DDepth = "nonlinearDisparity min=2 max=40 width=1920 model=0,20,7,0"
```

The views are drawn with a parallax for each sample value: the right eye's position of a point minus the left eye's, in pixels of the view, positive behind the screen.
`v` below is a sample divided by its largest value, `maxVal = 2^bits - 1` (H.265 G.14.3.4).

| Kind | A sample is | Keys | Defined in |
|---|---|---|---|
| `nearness` | larger nearer, uniform in parallax (2D-plus-depth) | `zero`, `range` | the common convention of 2D-plus-depth |
| `farness` | larger farther, uniform in parallax | `zero`, `range` | |
| `inverseZ` | uniform in 1/Z: 0 is 1/ZFar, maxVal is 1/ZNear | `near`, `far` (ZNear, ZFar) | ITU-T H.265 Table G.3 type 0; H.264 Table H-1 type 0 |
| `disparity` | uniform in disparity: 0 is DMin, maxVal is DMax | `min`, `max`, `width` | H.265 Table G.3 type 1 (DMin and DMax in luma sample widths of the view, G.14.3.4); H.264 Table H-1 type 1 |
| `z` | uniform in Z: 0 is ZFar, maxVal is ZNear | `near`, `far` | H.265 Table G.3 type 2; H.264 Table H-1 type 2 |
| `nonlinearDisparity` | disparity after DepthLUT | `min`, `max`, `width`, `model` | H.265 Table G.3 type 3 and equation G-10; H.264 Table H-1 type 3 and H.13.2.3 |
| `parallax` | a parallax of (m - parallaxZero) / parallaxScale centimetres on a screen wref centimetres wide | `parallaxZero`, `parallaxScale`, `wref` | ISO/IEC 23002-3 6.1.2.2 and 6.2.2.2, as HDMI 1.4a Table H-4 cites them (our reading: unverified) |
| `rangeLinear` | uniform in distance: 0 is Near, maxVal is Far | `near`, `far` | Google Dynamic Depth 1.0, Depth Map, Format "RangeLinear"; GDepth |
| `rangeInverse` | uniform in 1/distance: 0 is Near, maxVal is Far | `near`, `far` | Dynamic Depth 1.0, Format "RangeInverse"; GDepth |
| `metric` | the distance itself, in the sensor's unit; 0 is no measurement | `near`, `far` in the same unit | depth sensors (see below) |

Keys for every kind:

- `zero`: where the screen is, as a nearness from 0 (the farthest samples) to 1 (the nearest). Default 0.5.
- `range`: the largest parallax either side of the screen, in pixels of a view 1920 wide, for the kinds that carry no absolute parallax. Any value: there is no limit. The edition's default is 58, about one degree of visual angle for a viewer three picture heights away.
- `shift`: a parallax added to every point, in pixels of a view 1920 wide, which moves the whole scene behind (positive) or in front of the screen.

For `inverseZ`, `z`, `rangeLinear` and `metric`, Near and Far set how 1/distance is spread over the range; parallax follows 1/distance.
For `disparity` and `nonlinearDisparity`, the parallax is the negated disparity, scaled from a view `width` wide to the shown one (a point nearer than the cameras' convergence has a positive disparity).
`model` lists `depth_nonlinear_representation_model[0]` to `[depth_nonlinear_representation_num_minus1 + 2]`, the first and the last 0.
Without both ends of a range (`near` and `far`, `min` and `max`), a kind reads as `nearness` or `farness`, the way its samples run.

ISO/IEC 23002-3 is not freely available: the `parallax` formula is our reading, kept apart and marked unverified wherever it is used.

## The marks of other formats

Readers turn each format's own signalling into these marks, so a player needs one path for all of them.

| Source | `Stereo3DLayout` | `Stereo3DDepth` |
|---|---|---|
| H.264 or H.265 depth representation information SEI (H.264 H.13.2.3, payload 50; H.265 G.14.3.4, payload 177) | as the stream is packed | the type (0 to 3) and its ZNear, ZFar, DMin, DMax and model |
| H.265 depth auxiliary layer, AuxId 2 (Table F.2) | `depthSeparate` | from its depth representation information, else `nearness` |
| HEIF or AVIF depth item: `auxC` "urn:mpeg:hevc:2015:auxid:2", "urn:mpeg:avc:2015:auxid:2" or "urn:mpeg:mpegB:cicp:systems:auxiliary:depth", `auxl` reference (ISO/IEC 23008-12) | `depthSeparate` | Apple's depth description when the depth item has one (below); else the depth representation information SEI message of the `aux_subtype` (a 32-bit length, then NAL units each with a 32-bit length, as libheif reads it); else `nearness` |
| Apple's depth data description, XMP namespace `http://ns.apple.com/pixeldatainfo/1.0/` on the depth image (HEIF depth item, or a Multi-Picture Format image of a JPEG whose `AuxiliaryImageType` is `disparity` or `depth`) | `depthSeparate` | `NativeFormat` "hdis" or "fdis" (`kCVPixelFormatType_DisparityFloat16` and `Float32`, 1/m): `inverseZ` with `near` and `far` the inverses of the disparities at the largest sample and at 0; "hdep" or "fdep" (metres): `rangeLinear`; the stored samples `IntMinValue` to `IntMaxValue` stand for `FloatMinValue` to `FloatMaxValue`, linearly |
| Dynamic Depth 1.0 JPEG: the `Device`'s `Container` items after the picture, the `DepthMap` of the Depth Photo profile's camera (else the first camera with one), its item by `DepthURI` | `depthSeparate` | `rangeLinear` or `rangeInverse` with its `Near` and `Far` |
| GDepth (Google Camera's Lens Blur): `GDepth:Data`, the depth map in base64, usually in the extended XMP (XMP Specification Part 3, 1.1.3.1) | `depthSeparate` | `rangeLinear` or `rangeInverse` with `GDepth:Near` and `GDepth:Far` |
| MPEG-2 transport or program stream: auxiliary video stream, stream type 0x1E (ISO/IEC 13818-1 2.16, 2.6.74) | `depthSeparate` | a depth map: `nearness` with `zero` = kfar / (kfar + knear); a parallax map: `parallax` (both from si_rbsp, our reading) |
| Intel RealSense Z16 (a sample times the depth scale is metres) | `depthSeparate` | `metric` |
| ROS REP 118 16UC1 depth image (millimetres) | `depthSeparate` | `metric` |
| Android DEPTH16 (13 bits of range in millimetres, 3 bits of confidence) | `depthSeparate` | `metric`, the range bits only |

Apple writes a depth representation information SEI message in its HEIF depth items too, of type 1 (uniform disparity), with the 1/m values of its description in DMin and DMax.
H.265 G.14.3.4 counts DMin and DMax in luma samples of the view, so Apple's description is read first: read as the SEI message, the whole depth of an iPhone 13 Pro portrait spans 1.4 pixels of parallax in a view 1920 wide, where Apple's description spreads it over the user's range (116 pixels by default).

Some auxiliary images look like depth and are not: Apple's portrait effects matte (`urn:com:apple:photo:2018:aux:portraiteffectsmatte`, "not intended to faithfully preserve all gradations of depth in the scene": a people segmentation), its semantic segmentation mattes and HDR gain maps, and a Dynamic Depth `DepthMap` whose `ItemSemantic` is `Segmentation`.
Readers never take them for a depth map.

A depth map measured along each pixel's ray (Dynamic Depth's and GDepth's `MeasureType` "OpticRay") rather than along the optical axis is drawn as if it were along the axis; the difference grows towards the corners of the picture.

## Where the marks are kept

In pictures, the marks are text: the `Stereo3DLayout` and `Stereo3DDepth` keys of a PNG `tEXt` chunk, and the same names in the metadata the edition's readers report (`QImageReader::text()`, KFileMetaData's `stereo3dLayout`).

In Matroska, separate depth tracks have registered `TrackPlaneType` values in `TrackCombinePlanes` (below).
The layouts that pack one frame have assigned `StereoMode` values 15 through 20 since 26 October 2026 (below).
The edition also carries their layout as the track's simple tag `STEREO3D_LAYOUT`; the registry assignment alone does not change existing readers or writers.
The meaning of depth samples remains in `STEREO3D_DEPTH`, with the values above; a registered plane type does not replace that information.

## Rules for readers

1. Read the marks as written. An unknown `Stereo3DLayout` value means one picture; so does a malformed `Stereo3DDepth`.
2. Never take a picture for a picture with its depth by its shape or its colours: only a mark says so.
3. Draw the two views from the picture and its depth, each at the picture's size, and hand them on as any other stereo pair: on Sparky Stereo OS, full side by side, the left eye first.
4. On a screen that shows one picture, show the picture unchanged.
5. Every way of drawing that gives a different picture is the user's choice: the picture as the left eye (the default) or the middle, forward (the default), backward or layered warping, the fill of the disocclusions (background extension by default, depth-aided, Telea's inpainting, none), the smoothing of the depth (off by default), the range, the screen and the shift, and, when the file keeps both, the picture the views are drawn from (the picture as shown by default, or the original before the camera's effects).

## Registrations

The requests were submitted on 17 October 2026. Status verified against the live registry on 26 October 2026.

### Assigned: separate depth tracks

IANA registered these [Matroska Track Plane Types](https://www.iana.org/assignments/matroska#matroska-track-plane-types) on **20 October 2026**, under the First Come First Served procedure of [RFC 9559 section 27.17](https://www.rfc-editor.org/rfc/rfc9559#section-27.17).

| TrackPlaneType | Meaning |
| --- | --- |
| 3 | depth of the left eye plane |
| 4 | depth of the right eye plane |
| 5 | depth of the background plane |

The change controller is Daniel Ramos, and IANA cites the [immutable v1.9.0 version of this specification](https://github.com/Sparky-OS/stereo-layer/blob/v1.9.0/docs/depth-marks.md).
These planes combine with their picture tracks through `TrackCombinePlanes` ([section 18.8](https://www.rfc-editor.org/rfc/rfc9559#section-18.8)).
For one picture and its depth map, the picture uses the left-eye plane (0) and its depth uses plane type 3.
This registration assigns identifiers; it does not establish support in every muxer, demuxer or player.

<a id="pending-packed-picture-and-depth-layouts"></a>

### Assigned: packed picture-and-depth layouts

IANA assigned six [Matroska Stereo Modes](https://www.iana.org/assignments/matroska#matroska-stereo-modes) on **26 October 2026**, following designated-expert review under [RFC 9559 section 27.7](https://www.rfc-editor.org/rfc/rfc9559#section-27.7), Specification Required.
Daniel Ramos is the change controller, and the reference remains the immutable v1.9.0 specification linked above.

| StereoMode | Packed layout |
| --- | --- |
| 15 | picture beside depth, picture first |
| 16 | depth beside picture, depth first |
| 17 | picture above depth |
| 18 | depth above picture |
| 19 | four rows: picture, depth, graphics, graphics depth |
| 20 | four quarters: picture, depth, background, background depth |

The registry assigns identifiers; it does not establish support in every muxer, demuxer or player.
The allocation does not change this specification's depth sample conventions or the immutable v1.9.0 tag.

## Implementations

- [libstereo-policy](../src/policy/stereo-policy.h) reads the layouts (`stereo_policy_read_mark()`, `stereo_policy_is_depth_layout()`).
- [libstereo-dibr](../src/dibr/stereo-dibr.h) reads `Stereo3DDepth` (`stereo_depth_read_mark()`, `stereo_depth_write_mark()`), gives each sample its parallax (`stereo_depth_parallax_table()`), finds the parts of a frame packed in a depth layout (`stereo_dibr_unpack()`) and draws the views, in C and in GLSL ([stereo-dibr-gl.h](../src/dibr/stereo-dibr-gl.h)).
- [libstereo-layer](../src/picture/picture.h) draws marked pictures for the edition's viewers (`Picture::fromDepth()`), and loads them from any reader that gives them as above (`Picture::load()`, `Picture::loadPair()`, with the user's `DepthOptions` and `DepthPicture`).
- The edition's [KImageFormats](https://invent.kde.org/danielcamposramos/kimageformats/-/tree/stereo3d) reads HEIF and AVIF depth items, Dynamic Depth, GDepth and Apple's portrait JPEGs this way, and HEIF and AVIF stereo pair groups ('ster', ISO/IEC 23008-12:2025 6.8.5) as `multiViewLeftFirst` or `multiViewRightFirst`.
- The edition's FFmpeg exports the SEI messages, the HEIF and MPEG-2 depth signalling and the H.265 depth layer, and its `dibr` filter draws full side by side through libstereo-dibr.

## Credits

The depth conventions are those of ITU-T H.264 and H.265 (depth representation information), of Google's Dynamic Depth 1.0 (CC BY 4.0) and depthmap metadata (GDepth, CC BY 4.0), and of Apple's AVDepthData documentation.
The rendering follows Fehn 2004 (doi:10.1117/12.524762), Zhang and Tam 2005 (doi:10.1109/TBC.2005.846190), Zitnick et al. 2004 (doi:10.1145/1015706.1015766), Shade et al. 1998 (doi:10.1145/280814.280882), Daribo and Saito 2010 (doi:10.1109/TBC.2010.2044296) and Telea 2004 (doi:10.1080/10867651.2004.10487596).
