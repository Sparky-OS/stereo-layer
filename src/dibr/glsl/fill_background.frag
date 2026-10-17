// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_background(): each run of holes along a row takes its farther valid neighbour (the left one when
// they are as far), colour and parallax. A row with nothing to extend keeps its holes.
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform int u_width;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 state = texelFetch(u_state, at, 0);
    o_color = texelFetch(u_color, at, 0);
    o_state = state;
    if (state.y != 2.0)
        return;
    int l = -1, r = -1;
    for (int x = at.x - 1; x >= 0; x--)
        if (texelFetch(u_state, ivec2(x, at.y), 0).y != 2.0) {
            l = x;
            break;
        }
    for (int x = at.x + 1; x < u_width; x++)
        if (texelFetch(u_state, ivec2(x, at.y), 0).y != 2.0) {
            r = x;
            break;
        }
    if (l < 0 && r < 0)
        return;
    int c = l < 0 ? r : r < 0 ? l : (texelFetch(u_state, ivec2(l, at.y), 0).x >= texelFetch(u_state, ivec2(r, at.y), 0).x ? l : r);
    o_color = texelFetch(u_color, ivec2(c, at.y), 0);
    o_state = vec4(texelFetch(u_state, ivec2(c, at.y), 0).x, 0.0, state.z, state.w);
}
