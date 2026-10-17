// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c hole_background(): the parallax of what each hole uncovers, the farther of its nearest valid neighbours
// along the row (0 when the row has none), into the state's third component; other pixels keep their own parallax
// there.
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
    if (state.y != 2.0) {
        o_state = vec4(state.x, state.y, state.x, state.w);
        return;
    }
    bool has_l = false, has_r = false;
    float zl = 0.0, zr = 0.0;
    for (int x = at.x - 1; x >= 0; x--) {
        vec4 v = texelFetch(u_state, ivec2(x, at.y), 0);
        if (v.y != 2.0) {
            has_l = true;
            zl = v.x;
            break;
        }
    }
    for (int x = at.x + 1; x < u_width; x++) {
        vec4 v = texelFetch(u_state, ivec2(x, at.y), 0);
        if (v.y != 2.0) {
            has_r = true;
            zr = v.x;
            break;
        }
    }
    float z = has_l && has_r ? max(zl, zr) : has_l ? zl : has_r ? zr : 0.0;
    o_state = vec4(state.x, state.y, z, state.w);
}
