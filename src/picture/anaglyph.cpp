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

int anaglyphChannel(const QColor &color, Picture::AnaglyphColorPair pair, bool left, int channel)
{
    const int red = color.red();
    const int green = color.green();
    const int blue = color.blue();
    switch (pair) {
    case Picture::AnaglyphColorPair::RedCyan:
        if (left) {
            return channel == 0 ? red : (channel == 1 ? (red + green) / 2 : (red + blue) / 2);
        }
        return channel == 0 ? (red + green) / 2 : (channel == 1 ? green : blue);
    case Picture::AnaglyphColorPair::GreenMagenta:
        if (left) {
            return channel == 0 ? (red + green) / 2 : (channel == 1 ? green : (green + blue) / 2);
        }
        return channel == 0 ? red : (channel == 1 ? (red + green + blue) / 3 : blue);
    case Picture::AnaglyphColorPair::AmberBlue:
        if (left) {
            return channel == 0 ? red : (channel == 1 ? green : (green + blue) / 2);
        }
        return channel == 0 ? (red + blue) / 2 : (channel == 1 ? (green + blue) / 2 : blue);
    case Picture::AnaglyphColorPair::None:
        return 0;
    }
    return 0;
}
}

Picture Picture::fromAnaglyph(const QImage &image, AnaglyphColorPair pair)
{
    Picture result;
    if (image.isNull() || pair == AnaglyphColorPair::None) {
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
            result.left.setPixelColor(
                x, y, QColor(anaglyphChannel(color, pair, true, 0), anaglyphChannel(color, pair, true, 1), anaglyphChannel(color, pair, true, 2)));
            result.right.setPixelColor(
                x, y, QColor(anaglyphChannel(color, pair, false, 0), anaglyphChannel(color, pair, false, 1), anaglyphChannel(color, pair, false, 2)));
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
