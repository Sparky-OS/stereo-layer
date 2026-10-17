// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c sample_plane(): the picture sampled at the view's positions, linearly between the two nearest picture
// pixels of the row; a background layer (u_layer) shows where the picture uncovers. Outputs the colour and the state
// (parallax shown, 2 for a hole or 0, the hole's background parallax later, Telea's distance later).
uniform highp sampler2D u_picture;
uniform highp sampler2D u_map;
uniform highp sampler2D u_layer_picture;
uniform highp sampler2D u_layer_map;
uniform int u_layer;
uniform int u_width;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

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
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec2 m = texelFetch(u_map, at, 0).rg;
    if (m.x >= 0.0) {
        o_color = fetch(u_picture, m.x, at.y);
        o_state = vec4(m.y, 0.0, 0.0, 0.0);
        return;
    }
    if (u_layer != 0) {
        vec2 l = texelFetch(u_layer_map, at, 0).rg;
        if (l.x >= 0.0) {
            o_color = fetch(u_layer_picture, l.x, at.y);
            o_state = vec4(l.y, 0.0, 0.0, 0.0);
            return;
        }
    }
    o_color = vec4(0.0);
    o_state = vec4(m.y, 2.0, 0.0, 0.0);
}
