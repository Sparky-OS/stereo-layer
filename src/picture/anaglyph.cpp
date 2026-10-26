// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "picture.h"
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>

namespace Stereo
{
namespace
{
// A pair has two colour sides: one channel (red, green or blue) stands out from the other two, or the other two stand out
// from it (cyan, magenta or amber). A side counts where it leads by at least this many levels of 255, and a picture can
// only be an anaglyph of the pair when each side covers at least this share of the sample; a picture of one colour on a
// neutral ground has a single side.
constexpr int sideLevels = 8;
constexpr double sideShare = 0.01;

struct ColorEdges {
    int width = 0;
    int height = 0;
    std::array<QVector<double>, 3> channels;
    // per channel: the share of the sample where that channel leads the other two, and where it trails both
    std::array<std::array<double, 2>, 3> sides = {};
};

ColorEdges colorEdges(const QImage &image)
{
    const QImage sample = image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    ColorEdges edges;
    edges.width = std::max(0, sample.width() - 2);
    edges.height = sample.height();
    for (auto &channel : edges.channels) {
        channel.reserve(edges.width * edges.height);
    }
    for (int y = 0; y < edges.height; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < edges.width; ++x) {
            edges.channels[0].append(qRed(row[x + 2]) - qRed(row[x]));
            edges.channels[1].append(qGreen(row[x + 2]) - qGreen(row[x]));
            edges.channels[2].append(qBlue(row[x + 2]) - qBlue(row[x]));
        }
        for (int x = 0; x < sample.width(); ++x) {
            const int level[3] = { qRed(row[x]), qGreen(row[x]), qBlue(row[x]) };
            for (int channel = 0; channel < 3; ++channel) {
                const int first = level[(channel + 1) % 3];
                const int second = level[(channel + 2) % 3];
                edges.sides[channel][0] += level[channel] - std::max(first, second) >= sideLevels;
                edges.sides[channel][1] += std::min(first, second) - level[channel] >= sideLevels;
            }
        }
    }
    const double pixels = double(sample.width()) * sample.height();
    for (auto &side : edges.sides) {
        side[0] /= pixels;
        side[1] /= pixels;
    }
    return edges;
}

double channelCorrelation(const ColorEdges &edges, int firstChannel, int secondChannel, int offset, int limit)
{
    double firstSum = 0;
    double secondSum = 0;
    double product = 0;
    double firstSquare = 0;
    double secondSquare = 0;
    const int count = (edges.width - 2 * limit) * edges.height;
    if (count <= 0) {
        return 0;
    }
    const auto &first = edges.channels[firstChannel];
    const auto &second = edges.channels[secondChannel];
    for (int y = 0; y < edges.height; ++y) {
        for (int x = limit; x < edges.width - limit; ++x) {
            const int index = y * edges.width + x;
            const double a = first[index];
            const double b = second[index + offset];
            firstSum += a;
            secondSum += b;
            product += a * b;
            firstSquare += a * a;
            secondSquare += b * b;
        }
    }
    const double numerator = product - firstSum * secondSum / count;
    const double denominator
        = std::sqrt(std::max(0.0, firstSquare - firstSum * firstSum / count) * std::max(0.0, secondSquare - secondSum * secondSum / count));
    return denominator == 0 ? 0 : numerator / denominator;
}

double anaglyphScore(const ColorEdges &edges, Picture::AnaglyphColorPair pair)
{
    const int firstChannel = pair == Picture::AnaglyphColorPair::RedCyan ? 0 : (pair == Picture::AnaglyphColorPair::GreenMagenta ? 1 : 2);
    const int secondChannel = (firstChannel + 1) % 3;
    const int thirdChannel = (firstChannel + 2) % 3;
    const int limit = std::min(32, edges.width / 8);
    if (limit == 0 || std::min(edges.sides[firstChannel][0], edges.sides[firstChannel][1]) < sideShare) {
        return 0;
    }
    const auto correlation = [&](int offset) {
        return std::min(channelCorrelation(edges, firstChannel, secondChannel, offset, limit),
            channelCorrelation(edges, firstChannel, thirdChannel, offset, limit));
    };
    const double zero = correlation(0);
    double best = zero;
    for (int offset = -limit; offset <= limit; ++offset) {
        if (offset != 0) {
            best = std::max(best, correlation(offset));
        }
    }
    return best >= 0.2 ? best - zero : 0;
}

}

bool Picture::anaglyphTransform(AnaglyphColorPair pair, bool left, int weights[9], int divisors[3])
{
    if (!weights || !divisors) {
        return false;
    }
    std::fill_n(weights, 9, 0);
    std::fill_n(divisors, 3, 1);
    // Same integer equations used by fromAnaglyph; public so video consumers
    // need neither duplicate policy nor reduce high-precision video to RGB32.
    static constexpr int transforms[3][2][12] = {
        {{1,0,0, 1,1,0, 1,0,1, 1,2,2}, {1,1,0, 0,1,0, 0,0,1, 2,1,1}},
        {{1,1,0, 0,1,0, 0,1,1, 2,1,2}, {1,0,0, 1,1,1, 0,0,1, 1,3,1}},
        {{1,0,0, 0,1,0, 0,1,1, 1,1,2}, {1,0,1, 0,1,1, 0,0,1, 2,2,1}},
    };
    const int index = int(pair) - int(AnaglyphColorPair::RedCyan);
    if (index < 0 || index >= 3) {
        return false;
    }
    const int *transform = transforms[index][left ? 0 : 1];
    std::copy_n(transform, 9, weights);
    std::copy_n(transform + 9, 3, divisors);
    return true;
}

Picture Picture::fromAnaglyph(const QImage &image, AnaglyphColorPair pair)
{
    Picture result;
    if (image.isNull() || pair == AnaglyphColorPair::None) {
        return result;
    }
    int weights[2][9], divisors[2][3];
    if (!anaglyphTransform(pair, true, weights[0], divisors[0])
        || !anaglyphTransform(pair, false, weights[1], divisors[1])) {
        return result;
    }
    result.original = image;
    result.anaglyph = pair;
    result.source = Source::User;
    result.left = QImage(image.size(), QImage::Format_RGB32);
    result.right = QImage(image.size(), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const int rgb[3] = { color.red(), color.green(), color.blue() };
            for (int eye = 0; eye < 2; ++eye) {
                int reconstructed[3] = {};
                for (int channel = 0; channel < 3; ++channel) {
                    for (int source = 0; source < 3; ++source) {
                        reconstructed[channel] += weights[eye][3 * channel + source] * rgb[source];
                    }
                    reconstructed[channel] /= divisors[eye][channel];
                }
                (eye ? result.right : result.left).setPixelColor(x, y,
                    QColor(reconstructed[0], reconstructed[1], reconstructed[2]));
            }
        }
    }
    result.layout = Layout::SideBySide;
    result.approximateColor = true;
    result.eyeOrder = EyeOrder::LeftFirst;
    return result;
}

Picture::AnaglyphColorPair Picture::detectAnaglyph(const QImage &image)
{
    if (image.isNull()) {
        return AnaglyphColorPair::None;
    }
    const ColorEdges edges = colorEdges(image);
    AnaglyphColorPair bestPair = AnaglyphColorPair::None;
    double bestScore = 0;
    for (const auto pair : { AnaglyphColorPair::RedCyan, AnaglyphColorPair::GreenMagenta, AnaglyphColorPair::AmberBlue }) {
        const double score = anaglyphScore(edges, pair);
        if (score > bestScore) {
            bestScore = score;
            bestPair = pair;
        }
    }
    return bestScore >= 0.1 ? bestPair : AnaglyphColorPair::None;
}

}
