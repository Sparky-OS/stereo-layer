/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Independent writer using FFmpeg's public muxer metadata API. */
#include <libavformat/avformat.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 5)
        return 2;
    AVFormatContext *in = NULL, *out = NULL;
    if (avformat_open_input(&in, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(in, NULL) < 0
        || avformat_alloc_output_context2(&out, NULL, NULL, argv[2]) < 0)
        return 1;
    out->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    AVStream *stream = avformat_new_stream(out, NULL);
    if (!stream || avcodec_parameters_copy(stream->codecpar, in->streams[0]->codecpar) < 0)
        return 1;
    stream->time_base = in->streams[0]->time_base;
    size_t size;
    AVSphericalMapping *sphere = av_spherical_alloc(&size);
    int coverage = atoi(argv[3]);
    sphere->projection = coverage == 360 ? AV_SPHERICAL_EQUIRECTANGULAR : coverage == 6 ? AV_SPHERICAL_CUBEMAP : AV_SPHERICAL_EQUIRECTANGULAR_TILE;
    if (coverage == 180) {
        sphere->bound_left = 0x40000000;
        sphere->bound_right = 0x40000000;
    } else if (coverage == 90) {
        sphere->bound_left = 0x60000000;
        sphere->bound_right = 0x60000000;
    }
    av_packet_side_data_add(&stream->codecpar->coded_side_data, &stream->codecpar->nb_coded_side_data,
        AV_PKT_DATA_SPHERICAL, (uint8_t *)sphere, size, 0);
    if (atoi(argv[4])) {
        AVStereo3D *stereo = av_stereo3d_alloc_size(&size);
        stereo->type = AV_STEREO3D_SIDEBYSIDE;
        av_packet_side_data_add(&stream->codecpar->coded_side_data, &stream->codecpar->nb_coded_side_data,
            AV_PKT_DATA_STEREO3D, (uint8_t *)stereo, size, 0);
    }
    if (avio_open(&out->pb, argv[2], AVIO_FLAG_WRITE) < 0 || avformat_write_header(out, NULL) < 0)
        return 1;
    AVPacket *pkt = av_packet_alloc();
    while (av_read_frame(in, pkt) >= 0) {
        av_packet_rescale_ts(pkt, in->streams[0]->time_base, stream->time_base);
        pkt->stream_index = 0;
        if (av_interleaved_write_frame(out, pkt) < 0)
            return 1;
        av_packet_unref(pkt);
    }
    int error = av_write_trailer(out);
    av_packet_free(&pkt);
    avio_closep(&out->pb);
    avformat_free_context(out);
    avformat_close_input(&in);
    return error < 0;
}
