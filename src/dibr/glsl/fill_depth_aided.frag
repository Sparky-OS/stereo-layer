// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_depth_aided(), one ring: a hole beside a pixel that is not a hole takes the mean of the pixels
// within u_radius that are as far as its background (the depth-aided idea of Daribo and Saito 2010), weighted by
// 1 / distance squared. Pixels that do not change are discarded (the target holds a copy of the input), so an
// occlusion query tells whether the ring filled anything.
#define EDGE_STEP 1.0
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform ivec2 u_size;
uniform int u_radius;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

bool hole(ivec2 q) { return texelFetch(u_state, q, 0).y == 2.0; }

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 state = texelFetch(u_state, at, 0);
    if (state.y != 2.0)
        discard;
    bool edge = false;
    for (int dy = -1; dy <= 1 && !edge; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            ivec2 q = at + ivec2(dx, dy);
            if (all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, u_size)) && !hole(q)) {
                edge = true;
                break;
            }
        }
    if (!edge)
        discard;
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int dy = -u_radius; dy <= u_radius; dy++)
        for (int dx = -u_radius; dx <= u_radius; dx++) {
            ivec2 q = at + ivec2(dx, dy);
            if ((dx == 0 && dy == 0) || any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, u_size)))
                continue;
            vec4 sq = texelFetch(u_state, q, 0);
            if (sq.y == 2.0 || sq.z < state.z - EDGE_STEP)
                continue;
            float w = 1.0 / float(dx * dx + dy * dy);
            acc += w * texelFetch(u_color, q, 0);
            wsum += w;
        }
    if (wsum <= 0.0)
        discard;
    o_color = acc / wsum;
    o_state = vec4(state.z, 0.0, state.z, state.w);
}
