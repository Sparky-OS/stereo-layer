# HEIF stereo reader contract

ISO/IEC 23008-12:2025 section 6.8.5 defines `ster` as left and right image items.
Amendment 1 section 6.8.11 defines `stem` as left, right and a monoscopic fallback, which may repeat either eye.
These are the file's native entity groups, read through libheif.

The Qt image reader keeps its primary image at index zero, so ordinary 2D loading keeps a distinct monoscopic fallback.
A primary that is an eye uses the existing `multiViewLeftFirst` or `multiViewRightFirst` mark and other eye at index one.
When the primary is a separate fallback, the reader exposes left and right at indices one and two, with `Stereo3DLayout=multiViewLeftFirst` and `Stereo3DViewIndices=1,2`.
`Stereo3DViewIndices` is reader metadata mapping the file's view IDs to reader image numbers, not an additional file format mark.
Both indices must be distinct, nonnegative and within the reader's image count.
A mark or indices without decodable views never creates a stereo picture.
`Picture::load` and `loadPair` select these images while retaining the primary in `original`.
The explicit per-file eye correction and left/right-only choices continue to act on the actual stereo views.
No input file or attribute is changed.

Each HEIF item retains its own rotation and mirror information; absence of a transform on one view does not inherit a transform from the primary.
The labelled tests cover the primary in either eye or a distinct fallback, fallback aliases, unrelated and malformed groups and exact decoded samples.
No spatial-photo camera calibration is applied to the stored pixels by this reader path.
Apple's comfort/disparity and camera metadata remain in the unchanged file and need separate display-policy review.
