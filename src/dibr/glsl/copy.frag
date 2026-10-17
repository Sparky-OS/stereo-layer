// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// The picture as it is, the left eye when the picture is the left eye.
uniform highp sampler2D u_picture;
uniform int u_offset;
out vec4 o_color;

void main() { o_color = texelFetch(u_picture, ivec2(int(gl_FragCoord.x) - u_offset, int(gl_FragCoord.y)), 0); }
