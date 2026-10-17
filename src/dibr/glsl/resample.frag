// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c depth_to_parallax(), second part: the parallax at the picture's size, bilinear between the depth
// map's samples, the nearest one where a neighbour has no depth. Output (parallax, valid).
uniform highp sampler2D u_par;
uniform ivec2 u_depth_size;
uniform ivec2 u_size;
out vec4 o_value;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    if (u_depth_size == u_size) {
        o_value = vec4(texelFetch(u_par, at, 0).rg, 0.0, 0.0);
        return;
    }
    float fy = (float(at.y) + 0.5) * float(u_depth_size.y) / float(u_size.y) - 0.5;
    float fx = (float(at.x) + 0.5) * float(u_depth_size.x) / float(u_size.x) - 0.5;
    int y0 = int(floor(fy)), x0 = int(floor(fx));
    float wy = fy - float(y0), wx = fx - float(x0);
    int ya = clamp(y0, 0, u_depth_size.y - 1), yb = clamp(y0 + 1, 0, u_depth_size.y - 1);
    int xa = clamp(x0, 0, u_depth_size.x - 1), xb = clamp(x0 + 1, 0, u_depth_size.x - 1);
    vec2 aa = texelFetch(u_par, ivec2(xa, ya), 0).rg, ab = texelFetch(u_par, ivec2(xb, ya), 0).rg;
    vec2 ba = texelFetch(u_par, ivec2(xa, yb), 0).rg, bb = texelFetch(u_par, ivec2(xb, yb), 0).rg;
    if (aa.g < 0.5 || ab.g < 0.5 || ba.g < 0.5 || bb.g < 0.5) {
        o_value = vec4(texelFetch(u_par, ivec2(wx < 0.5 ? xa : xb, wy < 0.5 ? ya : yb), 0).rg, 0.0, 0.0);
        return;
    }
    float top = aa.r * (1.0 - wx) + ab.r * wx, bottom = ba.r * (1.0 - wx) + bb.r * wx;
    o_value = vec4(top * (1.0 - wy) + bottom * wy, 1.0, 0.0, 0.0);
}
