// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_invalid(), rows with no depth at all: the nearest row above with depth, else below, else the
// screen (parallax 0).
uniform highp sampler2D u_par;
uniform int u_height;
out vec4 o_value;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec2 here = texelFetch(u_par, at, 0).rg;
    if (here.g > 0.5) {
        o_value = vec4(here, 0.0, 0.0);
        return;
    }
    for (int y = at.y - 1; y >= 0; y--) {
        vec2 v = texelFetch(u_par, ivec2(at.x, y), 0).rg;
        if (v.g > 0.5) {
            o_value = vec4(v.r, 1.0, 0.0, 0.0);
            return;
        }
    }
    for (int y = at.y + 1; y < u_height; y++) {
        vec2 v = texelFetch(u_par, ivec2(at.x, y), 0).rg;
        if (v.g > 0.5) {
            o_value = vec4(v.r, 1.0, 0.0, 0.0);
            return;
        }
    }
    o_value = vec4(0.0, 1.0, 0.0, 0.0);
}
