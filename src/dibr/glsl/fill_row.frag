// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_invalid(), rows: a sample with no depth takes the farther (larger parallax) of its nearest
// valid neighbours along the row; a row with none stays without depth for fill_column.frag.
uniform highp sampler2D u_par;
uniform int u_width;
out vec4 o_value;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec2 here = texelFetch(u_par, at, 0).rg;
    if (here.g > 0.5) {
        o_value = vec4(here, 0.0, 0.0);
        return;
    }
    bool has_l = false, has_r = false;
    float zl = 0.0, zr = 0.0;
    for (int x = at.x - 1; x >= 0; x--) {
        vec2 v = texelFetch(u_par, ivec2(x, at.y), 0).rg;
        if (v.g > 0.5) {
            has_l = true;
            zl = v.r;
            break;
        }
    }
    for (int x = at.x + 1; x < u_width; x++) {
        vec2 v = texelFetch(u_par, ivec2(x, at.y), 0).rg;
        if (v.g > 0.5) {
            has_r = true;
            zr = v.r;
            break;
        }
    }
    if (has_l && has_r)
        o_value = vec4(max(zl, zr), 1.0, 0.0, 0.0);
    else if (has_l)
        o_value = vec4(zl, 1.0, 0.0, 0.0);
    else if (has_r)
        o_value = vec4(zr, 1.0, 0.0, 0.0);
    else
        o_value = vec4(0.0, 0.0, 0.0, 0.0);
}
