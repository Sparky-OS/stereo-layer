// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c fill_telea(), one ring: the holes at a distance up to u_ring + 1 take Telea's estimate (equations 4 to
// 8) from the known, band and filled pixels within u_radius that are as far as their background, weighted by
// direction, distance and level set distance, each source extrapolated by its gradient; the estimate stays within the
// sources' values. The ring order stands for the C code's heap order (pixels of one ring do not feed each other).
#define EDGE_STEP 1.0
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform ivec2 u_size;
uniform int u_radius;
uniform float u_ring;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

bool inside_size(ivec2 q) { return all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, u_size)); }
bool known(ivec2 q) { return inside_size(q) && texelFetch(u_state, q, 0).y < 1.5; }

// the gradient of a value at q from the known neighbours (gradient() of the C code)
vec4 grad_x(ivec2 q, out vec4 gy, bool distance)
{
    bool l = known(q + ivec2(-1, 0)), r = known(q + ivec2(1, 0)), u = known(q + ivec2(0, -1)), d = known(q + ivec2(0, 1));
    vec4 c = distance ? vec4(texelFetch(u_state, q, 0).w) : texelFetch(u_color, q, 0);
    vec4 fl = l ? (distance ? vec4(texelFetch(u_state, q + ivec2(-1, 0), 0).w) : texelFetch(u_color, q + ivec2(-1, 0), 0)) : c;
    vec4 fr = r ? (distance ? vec4(texelFetch(u_state, q + ivec2(1, 0), 0).w) : texelFetch(u_color, q + ivec2(1, 0), 0)) : c;
    vec4 fu = u ? (distance ? vec4(texelFetch(u_state, q + ivec2(0, -1), 0).w) : texelFetch(u_color, q + ivec2(0, -1), 0)) : c;
    vec4 fd = d ? (distance ? vec4(texelFetch(u_state, q + ivec2(0, 1), 0).w) : texelFetch(u_color, q + ivec2(0, 1), 0)) : c;
    vec4 gx = l && r ? (fr - fl) * 0.5 : r ? fr - c : l ? c - fl : vec4(0.0);
    gy = u && d ? (fd - fu) * 0.5 : d ? fd - c : u ? c - fu : vec4(0.0);
    return gx;
}

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 state = texelFetch(u_state, at, 0);
    if (state.y != 2.0 || state.w > u_ring + 1.0)
        discard;
    vec4 tgy;
    float tgx = grad_x(at, tgy, true).x;
    vec4 acc = vec4(0.0), lo = vec4(3.4e38), hi = vec4(-3.4e38);
    float wsum = 0.0;
    int r2 = u_radius * u_radius;
    for (int dy = -u_radius; dy <= u_radius; dy++)
        for (int dx = -u_radius; dx <= u_radius; dx++) {
            ivec2 p = at + ivec2(dx, dy);
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy > r2 || !inside_size(p))
                continue;
            vec4 sp = texelFetch(u_state, p, 0);
            if (sp.y > 1.5 || sp.z < state.z - EDGE_STEP)
                continue;
            float rx = float(-dx), ry = float(-dy), len2 = rx * rx + ry * ry, len = sqrt(len2);
            float dir = max(abs(rx * tgx + ry * tgy.x) / len, 1e-6);
            float dst = 1.0 / len2;
            float lev = 1.0 / (1.0 + abs(sp.w - state.w));
            vec4 igy;
            vec4 igx = grad_x(p, igy, false);
            vec4 value = texelFetch(u_color, p, 0);
            float w = dir * dst * lev;
            acc += w * (value + igx * rx + igy * ry);
            wsum += w;
            lo = min(lo, value);
            hi = max(hi, value);
        }
    if (wsum > 0.0) {
        o_color = clamp(acc / wsum, lo, hi);
    } else {
        // nothing as far around it yet: the first known neighbour
        o_color = texelFetch(u_color, at, 0);
        ivec2 nb[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, -1), ivec2(0, 1));
        for (int k = 3; k >= 0; k--)
            if (known(at + nb[k]))
                o_color = texelFetch(u_color, at + nb[k], 0);
    }
    o_state = vec4(state.z, 1.0, state.z, state.w);
}
