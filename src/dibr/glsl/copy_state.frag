// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// A copy of the colour and the state, which the passes that discard what does not change draw over.
uniform highp sampler2D u_color;
uniform highp sampler2D u_state;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_state;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    o_color = texelFetch(u_color, at, 0);
    o_state = texelFetch(u_state, at, 0);
}
