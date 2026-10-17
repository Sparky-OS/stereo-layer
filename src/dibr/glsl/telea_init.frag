// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_telea(), start (Telea 2004): holes are inside (2) at a distance of 1e6; a known pixel beside a
// hole starts the march (band, 1) unless it is nearer than that hole's background (blocked, 3); the others are known
// (0). Known, band and blocked pixels are at distance 0.
#define EDGE_STEP 1.0
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform ivec2 u_size;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 state = texelFetch(u_state, at, 0);
    o_color = texelFetch(u_color, at, 0);
    if (state.y == 2.0) {
        o_state = vec4(state.x, 2.0, state.z, 1e6);
        return;
    }
    bool beside = false, nearer = false;
    ivec2 nb[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));
    for (int k = 0; k < 4; k++) {
        ivec2 q = at + nb[k];
        if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, u_size)))
            continue;
        vec4 sq = texelFetch(u_state, q, 0);
        if (sq.y == 2.0) {
            beside = true;
            nearer = nearer || state.x < sq.z - EDGE_STEP;
        }
    }
    o_state = vec4(state.x, nearer ? 3.0 : beside ? 1.0 : 0.0, state.z, 0.0);
}
