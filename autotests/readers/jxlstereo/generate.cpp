// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

// Writes the JXLStereo and animated pair fixtures of picturetest and animatedpicturetest with libjxl's and libavif's
// encoders. Built and run once; the files it writes are kept beside it (stvi-source.avif only feeds the next command):
//   c++ -std=c++17 -fPIC generate.cpp -o generate $(pkg-config --cflags --libs Qt6Gui libjxl libavif) && ./generate DIR
//   python3 avif-stereo-track.py DIR/stvi-source.avif DIR/animated-stvi.avif && rm DIR/stvi-source.avif
// Every picture is lossless and 16 bits a sample. The left view is red and the right view blue; the green sample of both
// is 1000 times the frame number plus 1000, a value no 8-bit sample holds, so a test can tell the frame and the precision.
#include <QImage>
#include <QRgba64>
#include <avif/avif.h>
#include <jxl/encode.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
const std::string attribute = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                              R"(<rdf:Description rdf:about="" xmlns:jxlstereo="https://github.com/Sparky-OS/jxlstereo" )"
                              R"(jxlstereo:layout="sideBySideLeftFirst"/></rdf:RDF></x:xmpmeta>)";
const std::string element = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                            R"(<rdf:Description rdf:about="" xmlns:s="https://github.com/Sparky-OS/jxlstereo" )"
                            R"(xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:format>image/jxl</dc:format>)"
                            R"(<s:layout>sideBySideLeftFirst</s:layout></rdf:Description></rdf:RDF></x:xmpmeta>)";
const std::string unfamiliar = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                               R"(<rdf:Description rdf:about="" xmlns:jxlstereo="https://github.com/Sparky-OS/jxlstereo" )"
                               R"(jxlstereo:layout="sideBySideRightFirst"/></rdf:RDF></x:xmpmeta>)";
const std::string elsewhere = R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
                              R"(<rdf:Description rdf:about="" xmlns:other="urn:example:not-the-mark" )"
                              R"(other:layout="sideBySideLeftFirst"/></rdf:RDF></x:xmpmeta>)";

struct Box {
    std::string content;
    bool compress = false;
};

void require(bool ok, const char *what)
{
    if (!ok) {
        std::cerr << what << '\n';
        std::exit(EXIT_FAILURE);
    }
}

// The presented picture: two views of width / 2, red on the left and blue on the right, green telling the frame.
QImage packed(int width, int height, int frame)
{
    QImage image(width, height, QImage::Format_RGBA64);
    const quint16 green = quint16(1000 * (frame + 1));
    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<QRgba64 *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const bool right = x >= width / 2;
            row[x] = QRgba64::fromRgba64(right ? 4000 : 60000, green, right ? 60000 : 4000, 65535);
        }
    }
    return image;
}

// What the file must store so that orientation 6 (ISO/IEC 18181-1:2024 D.3.2, turn 90 degrees clockwise) presents image.
QImage storedForRotate90(const QImage &presented) { return presented.transformed(QTransform().rotate(-90)); }

void writeFile(const std::string &path, const uint8_t *bytes, size_t size)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(bytes), std::streamsize(size));
    require(bool(file), "write");
}

std::vector<uint8_t> jxl(const std::vector<QImage> &frames, const std::vector<int> &delays, int loops, int orientation,
    const std::vector<Box> &boxes)
{
    std::unique_ptr<JxlEncoder, decltype(&JxlEncoderDestroy)> encoder(JxlEncoderCreate(nullptr), JxlEncoderDestroy);
    auto *enc = encoder.get();
    require(JxlEncoderUseContainer(enc, JXL_TRUE) == JXL_ENC_SUCCESS && JxlEncoderUseBoxes(enc) == JXL_ENC_SUCCESS, "container");
    JxlBasicInfo info;
    JxlEncoderInitBasicInfo(&info);
    info.xsize = frames.front().width();
    info.ysize = frames.front().height();
    info.bits_per_sample = 16;
    info.num_extra_channels = 1;
    info.alpha_bits = 16;
    info.uses_original_profile = JXL_TRUE;
    info.orientation = JxlOrientation(orientation);
    info.have_animation = frames.size() > 1 ? JXL_TRUE : JXL_FALSE;
    info.animation.tps_numerator = 1000;
    info.animation.tps_denominator = 1;
    info.animation.num_loops = uint32_t(loops);
    require(JxlEncoderSetBasicInfo(enc, &info) == JXL_ENC_SUCCESS, "JxlEncoderSetBasicInfo");
    JxlColorEncoding colour;
    JxlColorEncodingSetToSRGB(&colour, JXL_FALSE);
    require(JxlEncoderSetColorEncoding(enc, &colour) == JXL_ENC_SUCCESS, "JxlEncoderSetColorEncoding");
    for (const Box &box : boxes) {
        require(JxlEncoderAddBox(enc, "xml ", reinterpret_cast<const uint8_t *>(box.content.data()), box.content.size(),
                    box.compress ? JXL_TRUE : JXL_FALSE)
                == JXL_ENC_SUCCESS,
            "JxlEncoderAddBox");
    }
    JxlEncoderCloseBoxes(enc);
    auto *settings = JxlEncoderFrameSettingsCreate(enc, nullptr);
    require(JxlEncoderSetFrameLossless(settings, JXL_TRUE) == JXL_ENC_SUCCESS, "lossless");
    const JxlPixelFormat format{4, JXL_TYPE_UINT16, JXL_NATIVE_ENDIAN, 0};
    for (size_t i = 0; i < frames.size(); ++i) {
        JxlFrameHeader header;
        JxlEncoderInitFrameHeader(&header);
        header.duration = uint32_t(delays.at(i));
        require(JxlEncoderSetFrameHeader(settings, &header) == JXL_ENC_SUCCESS, "JxlEncoderSetFrameHeader");
        require(JxlEncoderAddImageFrame(settings, &format, frames[i].constBits(), frames[i].sizeInBytes()) == JXL_ENC_SUCCESS,
            "JxlEncoderAddImageFrame");
    }
    JxlEncoderCloseInput(enc);
    std::vector<uint8_t> bytes(4096);
    size_t used = 0;
    for (;;) {
        auto *next = bytes.data() + used;
        size_t available = bytes.size() - used;
        const auto status = JxlEncoderProcessOutput(enc, &next, &available);
        used = bytes.size() - available;
        if (status == JXL_ENC_SUCCESS) {
            break;
        }
        require(status == JXL_ENC_NEED_MORE_OUTPUT, "JxlEncoderProcessOutput");
        bytes.resize(bytes.size() * 2);
    }
    bytes.resize(used);
    return bytes;
}

void still(const std::string &path, const QImage &image, const std::vector<Box> &boxes, int orientation = 1)
{
    const auto bytes = jxl({image}, {0}, 0, orientation, boxes);
    writeFile(path, bytes.data(), bytes.size());
}

void avifSequence(const std::string &path, const std::vector<QImage> &frames, int delay)
{
    std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)> encoder(avifEncoderCreate(), avifEncoderDestroy);
    require(bool(encoder), "avifEncoderCreate");
    encoder->codecChoice = AVIF_CODEC_CHOICE_AOM;
    encoder->maxThreads = 1;
    encoder->speed = 8;
    encoder->timescale = 1000;
    encoder->repetitionCount = AVIF_REPETITION_COUNT_INFINITE;
    encoder->quality = AVIF_QUALITY_LOSSLESS;
    encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    for (const auto &frame : frames) {
        std::unique_ptr<avifImage, decltype(&avifImageDestroy)> image(
            avifImageCreate(frame.width(), frame.height(), 12, AVIF_PIXEL_FORMAT_YUV444), avifImageDestroy);
        require(bool(image), "avifImageCreate");
        image->yuvRange = AVIF_RANGE_FULL;
        image->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
        image->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
        image->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_IDENTITY;
        avifRGBImage rgb;
        avifRGBImageSetDefaults(&rgb, image.get());
        rgb.depth = 16;
        rgb.format = AVIF_RGB_FORMAT_RGBA;
        rgb.pixels = const_cast<uint8_t *>(frame.constBits());
        rgb.rowBytes = uint32_t(frame.bytesPerLine());
        require(avifImageRGBToYUV(image.get(), &rgb) == AVIF_RESULT_OK, "avifImageRGBToYUV");
        require(avifEncoderAddImage(encoder.get(), image.get(), uint64_t(delay), AVIF_ADD_IMAGE_FLAG_NONE) == AVIF_RESULT_OK, "avifEncoderAddImage");
    }
    avifRWData result = AVIF_DATA_EMPTY;
    require(avifEncoderFinish(encoder.get(), &result) == AVIF_RESULT_OK, "avifEncoderFinish");
    writeFile(path, result.data, result.size);
    avifRWDataFree(&result);
}
}

int main(int argc, char **argv)
{
    require(argc == 2, "usage: generate DIR");
    const std::string dir = std::string(argv[1]) + '/';
    const QImage pair = packed(16, 4, 0);
    still(dir + "pair-xml-attribute.jxl", pair, {{attribute, false}});
    still(dir + "pair-xml-element.jxl", pair, {{element, false}});
    still(dir + "pair-brob-attribute.jxl", pair, {{attribute, true}});
    still(dir + "pair-brob-element.jxl", pair, {{element, true}});
    still(dir + "pair-rotate90.jxl", storedForRotate90(pair), {{attribute, false}}, 6);
    still(dir + "pair-odd-width.jxl", packed(15, 4, 0), {{attribute, false}});
    still(dir + "pair-declared-twice.jxl", pair, {{attribute, false}, {element, true}});
    still(dir + "pair-unfamiliar-value.jxl", pair, {{unfamiliar, false}});
    still(dir + "pair-other-namespace.jxl", pair, {{elsewhere, false}});
    still(dir + "unmarked-2to1.jxl", packed(16, 8, 0), {});
    std::vector<QImage> frames;
    for (int i = 0; i < 3; ++i) {
        frames.push_back(packed(16, 4, i));
    }
    const auto animated = jxl(frames, {100, 200, 300}, 2, 1, {{attribute, false}});
    writeFile(dir + "animated-pair.jxl", animated.data(), animated.size());
    // The same file without its last 40 bytes: the third frame cannot be decoded.
    writeFile(dir + "animated-pair-truncated.jxl", animated.data(), animated.size() - 40);
    // 4:1, outside the shape of an unmarked pair (Picture::pairByShape()), so only a mark can make it one.
    std::vector<QImage> wide;
    for (int i = 0; i < 3; ++i) {
        wide.push_back(packed(64, 16, i));
    }
    avifSequence(dir + "animated-sequence.avif", wide, 100);
    // Square views, which avif-stereo-track.py takes to write animated-stvi.avif; not kept.
    std::vector<QImage> square;
    for (int i = 0; i < 3; ++i) {
        square.push_back(packed(32, 16, i));
    }
    avifSequence(dir + "stvi-source.avif", square, 100);
    return 0;
}
