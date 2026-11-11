// SPDX-License-Identifier: GPL-2.0-or-later
#include <stereo-av.h>
#include <stereo-dibr.h>
#include <stereo-policy.h>

int main()
{
    StereoAVPair *pair = stereo_av_pair_alloc();
    if (!pair)
        return 1;
    stereo_av_pair_free(&pair);
    if (pair)
        return 2;
    stereo_dibr_options options;
    stereo_dibr_options_default(&options);
    if (!stereo_dibr_options_is_default(&options))
        return 3;
    if (!stereo_policy_is_depth_layout(STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE) || stereo_policy_is_depth_layout(STEREO_POLICY_LAYOUT_SIDE_BY_SIDE))
        return 4;
    return 0;
}
