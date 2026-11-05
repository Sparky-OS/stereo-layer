# YouTube's yt3d keywords: a site's mark for a video

Before the "This video is already in 3D" upload setting (2011), YouTube took a video's 3D layout from its keywords (tags).
Such tags remain on videos from that time, and video sites that carry YouTube's keywords on (Invidious, Piped) show them
as they are. They are a convention of one site, not a standard: the web has no mark for a video's stereo layout outside WebXR.
A reader of them is therefore a **site mark**, used only when the stream and its container say nothing, after them and before
the shape rule, and under the viewer's own choice:

1. the user's choice (the player's menu: packing, "Swap left and right eyes")
2. the stream's and the container's marks (frame packing SEI, Matroska StereoMode, MP4 st3d)
3. **the site's mark (yt3d keywords)**
4. the file name's tag, then the shape rule (at least three times as wide as high is side by side)

One reader, `stereo_policy_site_mark()` and `stereo_policy_site_mark_list()` in libstereo-policy (src/policy/stereo-policy-site.c),
serves the browser (Waterfox Stereo) and PlasmaTube, so a video is read alike wherever it is shown.

## The forms

Case, spaces around the name and value, and quotes do not matter; a malformed number is read as far as it can be.

| Tag | Meaning |
|---|---|
| `yt3d:enable=LR` | side by side, the left eye's view on the left (YouTube's recommended format) |
| `yt3d:enable=RL` | side by side, the right eye's view on the left |
| `yt3d:enable=LonR` | top and bottom, the left eye on top |
| `yt3d:enable=RonL` | top and bottom, the right eye on top |
| `yt3d:enable=true` (1, yes, on) | the first form: what `yt3d:left` and `yt3d:right` place; without them side by side, **right eye first** (YouTube's player took such a video for cross-eyed) |
| `yt3d:enable=false` (0, no, off) | the site says the video is not 3D |
| `yt3d:swap=true` (1, yes, on, or the bare tag) | the eyes the other way round, whatever else decided; with `enable=true` the left eye is first (parallel) |
| `yt3d:left=l_t_r_b`, `yt3d:right=l_t_r_b` | where each eye is, as the left, top, right and bottom fractions of the frame. The two halves of the frame, side by side or one above the other, either way round, are the arrangements of this policy; any other rectangle is not, and the keywords give no mark (return value 2, so the program can say so) |
| `yt3d:aspect=W:H` (W/H, a number such as 1.78) | the shape of ONE view as the site shows it. Passed to `stereo_policy_video_views()` as the plane size: a frame of that shape holds half views (squeezed, the YouTube norm), one twice as wide (side by side) or as tall (top and bottom) holds full views |

An explicit `enable=LR|RL|LonR|RonL` wins over rectangles, rectangles over the first form's side by side. `swap` alone, `aspect`
alone and an unknown `enable` value carry no arrangement (return value 2).

## Where each rule comes from

- The four `enable` words: YouTube's help page "Upload 3D video" of 2010, as quoted at stereo3dchannel.blogspot.com (2011-02-21):
  LR "side by side squashed, left eye frame on left, right eye on right"; RL the reverse; LonR and RonL top and bottom.
- `swap`: "the yt3d:swap tag was still necessary to get the player to recognize the left eye frame was on the left" (same page);
  Tim Dashwood, dvinfo.net, "if your video is in parallel format just add the tag yt3d:swap=true". YouTube's player showed an
  `enable=true` video cross-eyed until it was swapped (aenigmatis.com/3d-stereo/youtube-links.htm). Hence the first form is right
  eye first, and `swap=true` makes it left eye first. PlasmaTube's first patch took the first form as left eye first and `swap`
  as right eye first, the reverse of this; the shared reader corrects it.
- Measured, not only quoted (tests/eye-order.py, a ground-plane and near-object disparity test): "Kate in 3D!" (XTt_HGmTxf4,
  `left=0_0._0.5_1`, `right=0.5_0_1_1`) reads left half = left eye in 12 of 15 frames (the rectangles say the same); "3D Maestro"
  (uMTvaNomlWY, `enable=true`, `swap=true`) reads left half = left eye, the extended hands nearer than the face (hand minus face
  disparity negative in four of five frames), as `swap=true` says.
- Half or full views: `aspect` is a plane size in the sense of Matroska's DisplayWidth and DisplayHeight (RFC 9559 section 18.10):
  `stereo_policy_video_views()` reads it the same way.
