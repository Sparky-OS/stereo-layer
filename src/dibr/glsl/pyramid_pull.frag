// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_pyramid(), one level down: a hole takes the mean of the four texels of the level above that are nearest
// to it (weights 9, 3, 3, 1 of 16), those that are as far as its background only. What is not a hole passes through. The
// level 0 (u_first) is the render's colour and state, where a filled hole gets its background parallax as its own.
#define EDGE_STEP 1.0
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform highp sampler2D u_pcolor;
uniform highp sampler2D u_pstate;
uniform ivec2 u_psize;
uniform int u_first;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 s = texelFetch(u_state, at, 0);
    o_color = texelFetch(u_color, at, 0);
    o_state = s;
    float own_w = u_first != 0 ? (s.y == 2.0 ? 0.0 : 1.0) : s.y;
    float own_z = u_first != 0 ? s.z : s.x;
    if (own_w > 0.0)
        return;
    ivec2 p = at >> 1;
    ivec2 sg = ivec2((at.x & 1) != 0 ? 1 : -1, (at.y & 1) != 0 ? 1 : -1);
    vec4 acc = vec4(0.0);
    float ws = 0.0;
    for (int j = 0; j < 4; j++) {
        ivec2 q = p + ivec2((j & 1) != 0 ? sg.x : 0, (j & 2) != 0 ? sg.y : 0);
        if (q.x < 0 || q.y < 0 || q.x >= u_psize.x || q.y >= u_psize.y)
            continue;
        vec4 ps = texelFetch(u_pstate, q, 0);
        if (ps.y <= 0.0 || ps.x < own_z - EDGE_STEP)
            continue;
        float wq = ((j & 1) != 0 ? 1.0 : 3.0) * ((j & 2) != 0 ? 1.0 : 3.0);
        acc += wq * texelFetch(u_pcolor, q, 0);
        ws += wq;
    }
    if (ws > 0.0) {
        o_color = acc / ws;
        o_state = u_first != 0 ? vec4(own_z, 0.0, own_z, s.w) : vec4(own_z, 1.0, 0.0, 0.0);
    }
}
