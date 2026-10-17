// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c depth_to_parallax(), first part: the parallax table read at the depth map's size. The table is a
// 256-wide RG32F texture of (parallax, 1) per sample value, (0, 0) for a sample with no depth. The output is
// (parallax, 1), or (0, 0) for no depth.
#ifdef DEPTH_INTEGER
uniform highp usampler2D u_depth;
#else
uniform highp sampler2D u_depth;
#endif
uniform highp sampler2D u_table;
uniform int u_table_size;
out vec4 o_value;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
#ifdef DEPTH_INTEGER
    int sample_value = int(texelFetch(u_depth, at, 0).r);
#else
    int sample_value = int(floor(texelFetch(u_depth, at, 0).r * float(u_table_size - 1) + 0.5));
#endif
    sample_value = clamp(sample_value, 0, u_table_size - 1);
    o_value = vec4(texelFetch(u_table, ivec2(sample_value & 255, sample_value >> 8), 0).rg, 0.0, 0.0);
}
