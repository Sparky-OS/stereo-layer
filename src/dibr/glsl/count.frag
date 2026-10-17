// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// Counts the holes still at a finite distance (an occlusion query): Telea's rings go on while there are any.
uniform highp sampler2D u_state;
out vec4 o_color;

void main()
{
    vec4 s = texelFetch(u_state, ivec2(gl_FragCoord.xy), 0);
    if (s.y != 2.0 || s.w >= 1e5)
        discard;
    o_color = vec4(0.0);
}
