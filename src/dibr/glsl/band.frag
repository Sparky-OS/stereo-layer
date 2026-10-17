// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// stereo-dibr.c warp_layered_row(), the boundary layer (Zitnick et al. 2004): at each depth edge the foreground's last
// band_w pixels, opaque inside and fading to the edge, while the main layer takes there the background beyond the
// edge, its colour and parallax. The C loop walks the edges from left to right, a nearer edge taking a pixel from a
// farther one; each pixel replays that loop over the edges around it. Output (1 in the band, opacity, the picture
// position the main layer shows, the parallax the main layer has), or (0, 1, -1, parallax) outside the band.
#define EDGE_STEP 1.0
#define MAX_BAND 16
#define REACH (3 * MAX_BAND)
uniform highp sampler2D u_par;
uniform int u_width;
uniform int u_band;
out vec4 o_value;

float par(int x) { return texelFetch(u_par, ivec2(clamp(x, 0, u_width - 1), int(gl_FragCoord.y)), 0).r; }

void main()
{
    int px = int(gl_FragCoord.x);
    int reach = 3 * u_band;
    // the replayed state of the pixels px - reach .. px + reach
    int dist[2 * REACH + 1];
    float alpha_of[2 * REACH + 1], src_of[2 * REACH + 1], mp_of[2 * REACH + 1];
    for (int i = 0; i <= 2 * reach; i++) {
        dist[i] = u_band;
        alpha_of[i] = 1.0;
        src_of[i] = -1.0;
        mp_of[i] = par(px - reach + i);
    }
    for (int x = px - reach - 1; x <= px + reach; x++) {
        if (x < 0 || x + 1 >= u_width)
            continue;
        float step_ = par(x + 1) - par(x);
        if (abs(step_) <= EDGE_STEP)
            continue;
        int fg = step_ > 0.0 ? x : x + 1, bg = step_ > 0.0 ? x + 1 : x, dir = step_ > 0.0 ? -1 : 1;
        float pfg = par(fg), pbg = par(bg);
        for (int k = 0; k < u_band; k++) {
            int q = fg + dir * k;
            int i = q - (px - reach);
            if (q < 0 || q >= u_width || i < 0 || i > 2 * reach || k >= dist[i])
                break;
            if (abs(par(q) - pfg) > EDGE_STEP)
                break;
            dist[i] = k;
            alpha_of[i] = (float(k) + 1.0) / (float(u_band) + 1.0);
            src_of[i] = float(bg) + 0.5;
            mp_of[i] = pbg;
        }
    }
    int c = reach;
    o_value = vec4(src_of[c] >= 0.0 ? 1.0 : 0.0, alpha_of[c], src_of[c], mp_of[c]);
}
