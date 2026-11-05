// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_pyramid(), one level up: each texel is the mean of the pixels of its four children that are as far
// as the farthest of them (the background of what the texel covers). Output state: x the parallax (a texel without a
// pixel of its own keeps the farthest background parallax of its holes), y the weight (the number of pixels, 0 for a hole).
// The first level is read from the render's colour and state (a hole is state.y == 2, its background parallax state.z).
#define EDGE_STEP 1.0
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform ivec2 u_size;
uniform int u_first;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 c[4];
    float z[4], w[4];
    float zmax = -3.4e38, zhole = -3.4e38;
    bool any = false;
    int n = 0;
    for (int j = 0; j < 4; j++) {
        ivec2 q = 2 * at + ivec2(j & 1, j >> 1);
        if (q.x >= u_size.x || q.y >= u_size.y)
            continue;
        vec4 s = texelFetch(u_state, q, 0);
        c[n] = texelFetch(u_color, q, 0);
        if (u_first != 0) {
            z[n] = s.z;
            w[n] = s.y == 2.0 ? 0.0 : 1.0;
        } else {
            z[n] = s.x;
            w[n] = s.y;
        }
        if (w[n] > 0.0) {
            any = true;
            zmax = max(zmax, z[n]);
        } else {
            zhole = max(zhole, z[n]);
        }
        n++;
    }
    vec4 acc = vec4(0.0);
    float ws = 0.0;
    if (any)
        for (int j = 0; j < n; j++)
            if (w[j] > 0.0 && z[j] >= zmax - EDGE_STEP) {
                acc += w[j] * c[j];
                ws += w[j];
            }
    o_color = ws > 0.0 ? acc / ws : vec4(0.0);
    o_state = vec4(any ? zmax : zhole, ws, 0.0, 0.0);
}
