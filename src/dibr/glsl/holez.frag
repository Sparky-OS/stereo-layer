// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c background_parallax_row() and warp_backward_row(): a hole of the view gets the parallax of the farther
// of its nearest neighbours along the row (what it uncovers); with backward warping (u_backward) every other pixel is
// sampled from the picture where the parallax it received says. Output (picture position or -1, parallax).
uniform highp sampler2D u_splat;
uniform int u_width;
uniform int u_backward;
uniform float u_s;
out vec4 o_value;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 here = texelFetch(u_splat, at, 0);
    if (here.w > 0.5) {
        float u = here.x;
        if (u_backward != 0)
            u = clamp(float(at.x) + 0.5 - u_s * here.y, 0.0, float(u_width));
        o_value = vec4(u, here.y, 0.0, 0.0);
        return;
    }
    bool has_l = false, has_r = false;
    float zl = 0.0, zr = 0.0;
    for (int x = at.x - 1; x >= 0; x--) {
        vec4 v = texelFetch(u_splat, ivec2(x, at.y), 0);
        if (v.w > 0.5) {
            has_l = true;
            zl = v.y;
            break;
        }
    }
    for (int x = at.x + 1; x < u_width; x++) {
        vec4 v = texelFetch(u_splat, ivec2(x, at.y), 0);
        if (v.w > 0.5) {
            has_r = true;
            zr = v.y;
            break;
        }
    }
    float z = has_l && has_r ? max(zl, zr) : has_l ? zl : has_r ? zr : 0.0;
    o_value = vec4(-1.0, z, 0.0, 0.0);
}
