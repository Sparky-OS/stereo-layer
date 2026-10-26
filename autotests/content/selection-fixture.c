/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Independent writer using FFmpeg's public muxer metadata API. */
#include <libavformat/avformat.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    AVFormatContext *in = NULL, *out = NULL;
    if (avformat_open_input(&in, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(in, NULL) < 0
        || avformat_alloc_output_context2(&out, NULL, NULL, argv[2]) < 0)
        return 1;
    out->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    AVStream *stream = NULL;
    for (int i = 0; i < 2; ++i) {
    stream = avformat_new_stream(out, NULL);
    if (!stream || avcodec_parameters_copy(stream->codecpar, in->streams[0]->codecpar) < 0)
        return 1;
    stream->time_base = in->streams[0]->time_base;
    if (i == atoi(argv[3])) {
        size_t size;
        AVSphericalMapping *sphere = av_spherical_alloc(&size);
        sphere->projection = AV_SPHERICAL_EQUIRECTANGULAR;
        av_packet_side_data_add(&stream->codecpar->coded_side_data, &stream->codecpar->nb_coded_side_data,
            AV_PKT_DATA_SPHERICAL, (uint8_t *)sphere, size, 0);
    }
    }
    if (avio_open(&out->pb, argv[2], AVIO_FLAG_WRITE) < 0 || avformat_write_header(out, NULL) < 0)
        return 1;
    AVPacket *pkt = av_packet_alloc();
    while (av_read_frame(in, pkt) >= 0) {
        for (int i = 0; i < 2; ++i) {
            AVPacket *copy = av_packet_clone(pkt);
            if (!copy) return 1;
            av_packet_rescale_ts(copy, in->streams[0]->time_base, out->streams[i]->time_base);
            copy->stream_index = i;
            int ret = av_interleaved_write_frame(out, copy);
            av_packet_free(&copy);
            if (ret < 0) return 1;
        }
        av_packet_unref(pkt);
    }
    int error = av_write_trailer(out);
    av_packet_free(&pkt);
    avio_closep(&out->pb);
    avformat_free_context(out);
    avformat_close_input(&in);
    return error < 0;
}
