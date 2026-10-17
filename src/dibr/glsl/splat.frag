// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c splat_row(), gathered: each view pixel t looks at the picture pixels that can land on it, from left to
// right, and keeps the nearest (smallest parallax), the first of equals, as the C loop does. Picture pixel x lands on
// [x + s p(x), x + 1 + s p(x + 1)), spread over it while it is at most u_stretch pixels wide, else on the one pixel at
// its centre. Modes: 0 the picture; 1 the layered warp's main layer (the band's pixels at the background's position
// and parallax); 2 the boundary layer (the band's pixels only, with their opacity). Pixels with no depth are skipped.
// Output (picture position at the pixel's left edge scale, parallax, opacity, 1), or (-1, 3.4e38, 0, 0) for a hole.
uniform highp sampler2D u_par;
uniform highp sampler2D u_band;
uniform int u_mode;
uniform float u_s;
uniform float u_stretch;
uniform int u_lo;
uniform int u_hi;
uniform int u_width;
out vec4 o_value;

vec4 band(int x) { return texelFetch(u_band, ivec2(x, int(gl_FragCoord.y)), 0); }

void main()
{
    int t = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    float best_z = 3.4e38, best_u = -1.0, best_a = 0.0;
    bool hit = false;
    for (int x = max(t + u_lo, 0); x <= min(t + u_hi, u_width - 1); x++) {
        vec2 px = texelFetch(u_par, ivec2(x, y), 0).rg;
        if (px.g < 0.5)
            continue;
        vec4 bx = u_mode != 0 ? band(x) : vec4(0.0, 1.0, -1.0, px.r);
        if (u_mode == 2 && bx.x < 0.5)
            continue;
        float p = u_mode == 1 ? bx.w : px.r;
        float pn = p;
        if (x + 1 < u_width) {
            vec2 pnx = texelFetch(u_par, ivec2(x + 1, y), 0).rg;
            vec4 bn = u_mode != 0 ? band(x + 1) : vec4(0.0);
            bool take_next = u_mode != 2 || bn.x > 0.5;
            if (take_next && pnx.g > 0.5)
                pn = u_mode == 1 ? bn.w : pnx.r;
        }
        float a = float(x) + u_s * p, b = float(x + 1) + u_s * pn;
        float u0 = float(x), u1 = float(x + 1);
        if (u_mode == 1 && bx.x > 0.5) {
            u0 = bx.z;
            u1 = bx.z;
        }
        if (b > a && b - a <= u_stretch) {
            int t0 = int(ceil(a - 0.5)), t1 = int(ceil(b - 0.5));
            if (t >= t0 && t < t1) {
                float f = (float(t) + 0.5 - a) / (b - a);
                float z = p + f * (pn - p);
                if (z < best_z) {
                    best_z = z;
                    best_u = u0 + f * (u1 - u0);
                    best_a = bx.y;
                    hit = true;
                }
            }
        } else if (int(floor(a + 0.5)) == t && p < best_z) {
            best_z = p;
            best_u = 0.5 * (u0 + u1);
            best_a = bx.y;
            hit = true;
        }
    }
    o_value = hit ? vec4(best_u, best_z, best_a, 1.0) : vec4(-1.0, 3.4e38, 0.0, 0.0);
}
