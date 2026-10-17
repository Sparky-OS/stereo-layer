// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c gaussian_pass(): one direction of the separable Gaussian (Zhang and Tam 2005), the edges clamped.
uniform highp sampler2D u_par;
uniform ivec2 u_size;
uniform ivec2 u_step;
uniform float u_sigma;
out vec4 o_value;

void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec2 here = texelFetch(u_par, at, 0).rg;
    if (u_sigma < 0.01) {
        o_value = vec4(here, 0.0, 0.0);
        return;
    }
    int r = int(ceil(3.0 * u_sigma));
    float sum = 0.0, acc = 0.0;
    for (int i = -r; i <= r; i++) {
        float k = exp(-0.5 * float(i * i) / (u_sigma * u_sigma));
        ivec2 q = clamp(at + u_step * i, ivec2(0), u_size - 1);
        sum += k;
        acc += k * texelFetch(u_par, q, 0).r;
    }
    o_value = vec4(acc / sum, here.g, 0.0, 0.0);
}
