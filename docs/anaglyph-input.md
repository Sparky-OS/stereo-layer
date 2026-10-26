# Anaglyph input reconstruction

`Picture::anaglyphTransform` exposes the same encoded-RGB equations as
`Picture::fromAnaglyph`. It is an additive API for consumers that already have
GPU textures or samples wider than the still-image function's RGB32 result.
Each of the three output channels is the dot product of one row of weights
with the input RGB triplet, divided by that row's divisor. The CPU picture
consumer uses integer truncation; a floating point consumer retains fractional
results until its final output quantization. No spatial resize is involved.

Red/cyan keeps the red channel for the left eye and green/blue for the right.
Green/magenta keeps green for the left and red/blue for the right. Amber/blue
keeps red/green for the left and blue for the right. The other channels remain
the existing approximate mixtures of encoded channels. Eye reversal is a
separate input correction. None and unknown colour pairs are rejected.

This does not invert an arbitrary anaglyph encoder. Missing colour channels,
clipping, colour transforms and cross-talk baked into an encoded source are
irreversible. A mixed channel can contain information from the other eye;
these equations are the existing shared approximation, not a recovery of the
original two full-colour images. The API does not perform transfer decoding,
colour-primary conversion, gamut mapping, or tone mapping. Consumers keep
those operations and metadata separate from eye identity and sample geometry.

The independent regression checks both consumers against explicit integer
equations for all three pairs, both eyes, 8192 encoded RGB values, and invalid
pairs. Existing still-image results are preserved exactly.
