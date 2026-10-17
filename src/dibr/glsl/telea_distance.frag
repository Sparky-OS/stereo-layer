// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_telea() and solve(), the distance: the fast marching method's eikonal step (Telea 2004, section 3)
// relaxed in parallel until nothing changes, each hole taking the least of the four solutions from its known, band or
// already reached neighbours (blocked pixels never count). Changed pixels are written, the others discarded.
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform ivec2 u_size;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

bool usable(ivec2 q, out float t)
{
    if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, u_size)))
        return false;
    vec4 s = texelFetch(u_state, q, 0);
    t = s.w;
    return s.y < 2.5 && (s.y != 2.0 || s.w < 1e5);
}

float solve(ivec2 q1, ivec2 q2)
{
    float t1 = 0.0, t2 = 0.0;
    bool k1 = usable(q1, t1), k2 = usable(q2, t2);
    float sol = 1e6;
    if (k1 && k2) {
        if (abs(t1 - t2) >= 1.0) {
            sol = 1.0 + min(t1, t2);
        } else {
            float sr = sqrt(2.0 - (t1 - t2) * (t1 - t2));
            float s = (t1 + t2 - sr) / 2.0;
            if (s >= t1 && s >= t2)
                sol = s;
            else if ((s += sr) >= t1 && s >= t2)
                sol = s;
        }
    } else if (k1) {
        sol = 1.0 + t1;
    } else if (k2) {
        sol = 1.0 + t2;
    }
    return sol;
}

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 state = texelFetch(u_state, at, 0);
    if (state.y != 2.0)
        discard;
    float t = min(min(solve(at + ivec2(-1, 0), at + ivec2(0, -1)), solve(at + ivec2(1, 0), at + ivec2(0, -1))),
        min(solve(at + ivec2(-1, 0), at + ivec2(0, 1)), solve(at + ivec2(1, 0), at + ivec2(0, 1))));
    if (t >= state.w - 1e-4)
        discard;
    o_color = texelFetch(u_color, at, 0);
    o_state = vec4(state.xyz, t);
}
