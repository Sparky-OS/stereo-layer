// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_rest(): what no fill reached takes the nearest pixel above that is not a hole, else the value of
// a hole.
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform vec4 u_hole;
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
    o_color = u_hole;
    for (int y = at.y - 1; y >= 0; y--)
        if (texelFetch(u_state, ivec2(at.x, y), 0).y != 2.0) {
            o_color = texelFetch(u_color, ivec2(at.x, y), 0);
            break;
        }
    o_state = vec4(state.x, 0.0, state.z, state.w);
}
