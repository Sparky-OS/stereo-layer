# Saving a correction without recompressing JPEG

Call `Stereo::Picture::saveCorrection(path, options)` only after the person accepts **Save to file**.
It never runs during loading. The caller owns its per-file settings: on success clear the saved packing, eye order and swap;
on failure keep them and show `SaveResult::error`. The old shared swap xattr is consumed on success.

`Options::inputLayout` / `inputOrder` select an absolute interpretation, overriding an incorrect declaration.
`None` means use the current declaration or normal picture policy. `swapEyes` reverses that interpretation;
it is a correction, so applying it repeatedly would swap back. After success use default options to reload.
`View::Mono` writes a mono JPS declaration. LeftOnly/RightOnly retain the complete pair.
An explicit red/cyan choice uses `anaglyph` and `inputOrder`; the other anaglyph colour pairs have no JPS descriptor.

```cpp
Stereo::Picture::Options correction;
correction.swapEyes = true;
const auto saved = Stereo::Picture::saveCorrection(path, correction); // person's save action
if (saved.success) {
    clearPerFileCorrection(); // application's existing settings store
    reloadPicture();
} else {
    showError(saved.error);
}
```

The codec is in `libstereo-policy`, `stereo-jpeg.h`, shared with KImageFormats. Applications never construct descriptor bytes.
The format plugin still owns encoded image saves; this API owns metadata-only file transactions.

## Format sources

* VRex, *General Purpose Stereoscopic Data Descriptor* (1997), pp.3–6,
  [archived original](https://web.archive.org/web/20111030182549id_/http://www.vrex.com/developer/sterdesc.pdf),
  [identical mirror](https://paulbourke.org/stereoscopy/stereoimage/spec.pdf).
  APP3 contains `_JPSJPS_`, BE16 descriptor length, BE32 descriptor, optional descriptor extension,
  BE16 comment length, ASCII comment. Type 0 is mono (display byte 0/1/2); type 1 has layout
  1 rows, 2 side by side, 3 over/under, 4 red/cyan. Flags 0/1/2 are half height, half width,
  left-first/red-left. The high byte is separation for side by side or over/under, otherwise zero.
  Existing half-size flags, compatible separation, mono display, extension bytes and comment are retained.
  Reserved or unknown fields are refused. The historical sView trailing NUL is accepted and exact lengths are emitted on changes.
* CIPA, *Multi-Picture Format*, DC-007-Translation-2009,
  [published specification mirrored](https://seos.fr/blog/doc/DC-007_E.pdf), §§5.2.3, 5.2.4.2, 5.2.4.5 and 6.2.2.2.
  MPF TIFF offsets are relative to the TIFF header, both byte orders. A pair has two JPEG disparity entries
  (MP type `020002.H`), B101 LONG count 1 numbers its viewpoints from left to right.
  B204 LONG count 1 identifies the base viewpoint for calibration. Renumber both B101 ordinals and both B204 references
  together, preserving the physical base camera and its calibration values. MPEntry offsets, sizes, dependency references,
  and every other attribute remain byte-identical. An existing complete pair with valid B101/B204 fields is required;
  creating missing MPF structures would require relocating all indexed metadata and is refused.
* ITU-T T.81 (1992), [Annex B](https://www.w3.org/Graphics/JPEG/itu-t81.pdf): JPEG markers,
  segment lengths, SOS scans, FF00 byte stuffing and restart markers. All scans through EOI are parsed;
  fake signatures inside metadata or entropy are never treated as descriptors.

## Transaction and limits

The complete original is validated and decoded. JPS changes only its APP3 descriptor, adding it after the initial APP0/APP1
segments if absent. MPO changes only the four LONG fields above. Encoded JPEG scans are never passed to an encoder.
A temporary file in the same directory is written, flushed, read byte-for-byte and reopened through the format reader;
the chosen layout and both decoded eyes must match before replacement. Ownership, permissions, ACLs and other xattrs
are preserved; failure aborts. A concurrent modification detected before replacement also aborts. Replacement uses atomic
rename. This is not a lock against another program writing in the last interval before rename; callers should serialize saves.

Ambiguous descriptors, truncated segments/scans, invalid/cyclic TIFF directories, overlapping or out-of-range images,
non-disparity MPOs, unsupported packings and unindexed trailing JPEG data are refused with a message.
Lossless JPEG (SOF3/7/11/15) is currently refused: the installed picture decoder is not a verification path for it.
This is not a JPS restriction or a need for DQT; APP3 insertion itself has no DQT dependency.
Symbolic links are refused. A hard-link path is atomically replaced; other hard links retain the original inode.
The operation keeps the file contents and metadata in memory; it targets photographs, not arbitrarily large data streams.

`picturesavetest` checks layout/order read-back, the JPS and MPO fields independently, every byte outside the descriptor,
progressive scans, refusal without file changes, and consumption of the legacy swap attribute.
