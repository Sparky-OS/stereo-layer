// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c render(), the end of a view: holes left at the value of a hole when nothing fills them, the layered
// warp's boundary layer over the filled main layer unless the main layer shows something nearer there, graphics
// (an overlay layer) over everything where they are; written into the eye's half of the target.
#define EDGE_STEP 1.0
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
uniform highp sampler2D u_map;
uniform highp sampler2D u_boundary;
uniform highp sampler2D u_picture;
uniform highp sampler2D u_layer_picture;
uniform highp sampler2D u_layer_map;
uniform int u_layered;
uniform int u_overlay;
uniform vec4 u_hole;
uniform int u_width;
uniform int u_offset;
out vec4 o_color;

vec4 fetch(highp sampler2D picture, float u, int y)
{
    float c = clamp(u - 0.5, 0.0, float(u_width - 1));
    int i0 = int(c);
    int i1 = i0 + 1 < u_width ? i0 + 1 : i0;
    float f = c - float(i0);
    return texelFetch(picture, ivec2(i0, y), 0) * (1.0 - f) + texelFetch(picture, ivec2(i1, y), 0) * f;
}

void main()
{
    ivec2 at = ivec2(int(gl_FragCoord.x) - u_offset, int(gl_FragCoord.y));
    vec4 color = texelFetch(u_color, at, 0);
    if (texelFetch(u_state, at, 0).y == 2.0)
        color = u_hole;
    if (u_layered != 0) {
        vec4 b = texelFetch(u_boundary, at, 0);
        vec2 m = texelFetch(u_map, at, 0).rg;
        if (b.w > 0.5 && !(m.x >= 0.0 && m.y < b.y - EDGE_STEP))
            color = b.z * fetch(u_picture, b.x, at.y) + (1.0 - b.z) * color;
    }
    if (u_overlay != 0) {
        vec2 l = texelFetch(u_layer_map, at, 0).rg;
        if (l.x >= 0.0)
            color = fetch(u_layer_picture, l.x, at.y);
    }
    o_color = color;
}
