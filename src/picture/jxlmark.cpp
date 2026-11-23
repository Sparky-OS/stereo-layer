// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "jxlmark_p.h"

#include <QBuffer>
#include <QIODevice>
#include <QList>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtEndian>
#include <brotli/decode.h>
#include <optional>

namespace Stereo
{
namespace
{
// ISO/IEC 18181-2:2026 5: the JPEG XL signature box, then the file type box.
const QByteArray signature = QByteArray::fromHex("0000000c4a584c200d0a870a");
constexpr QLatin1StringView rdfNamespace("http://www.w3.org/1999/02/22-rdf-syntax-ns#");
constexpr QLatin1StringView markNamespace("https://github.com/Sparky-OS/jxlstereo");
constexpr QLatin1StringView markValue("sideBySideLeftFirst");
// An XMP packet is small; a box that claims more is not read, and no Brotli stream may inflate past this.
constexpr qint64 packetLimit = 16 * 1024 * 1024;

std::optional<QByteArray> inflateBrotli(const QByteArray &compressed)
{
    BrotliDecoderState *state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state) {
        return std::nullopt;
    }
    size_t availableIn = size_t(compressed.size());
    const auto *nextIn = reinterpret_cast<const uint8_t *>(compressed.constData());
    QByteArray result;
    std::optional<QByteArray> answer;
    uint8_t chunk[65536];
    for (;;) {
        size_t availableOut = sizeof chunk;
        uint8_t *nextOut = chunk;
        const BrotliDecoderResult status = BrotliDecoderDecompressStream(state, &availableIn, &nextIn, &availableOut, &nextOut, nullptr);
        result.append(reinterpret_cast<const char *>(chunk), qsizetype(sizeof chunk - availableOut));
        if (result.size() > packetLimit) {
            break;
        }
        if (status == BROTLI_DECODER_RESULT_SUCCESS) {
            answer = result;
            break;
        }
        if (status != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            break;
        }
    }
    BrotliDecoderDestroyInstance(state);
    return answer;
}

// The declarations of one XMP packet: an attribute of a top-level rdf:Description (the file's own description, ISO
// 16684-1:2019 7.4), or a child element of it holding a text value (7.5). Anything else in the namespace is not file-level.
void scanPacket(const QByteArray &xml, QStringList &values, QStringList &problems)
{
    enum class Kind { Other, Rdf, Description };
    QXmlStreamReader reader(xml);
    QList<Kind> open;
    const qsizetype before = values.size();
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement) {
            if (!open.isEmpty()) {
                open.removeLast();
            }
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const Kind parent = open.isEmpty() ? Kind::Other : open.last();
        if (reader.namespaceUri() == rdfNamespace && reader.name() == QLatin1String("RDF")) {
            open.append(Kind::Rdf);
        } else if (parent == Kind::Rdf && reader.namespaceUri() == rdfNamespace && reader.name() == QLatin1String("Description")) {
            open.append(Kind::Description);
            if (reader.attributes().hasAttribute(markNamespace, QLatin1String("layout"))) {
                values.append(reader.attributes().value(markNamespace, QLatin1String("layout")).toString());
            }
        } else if (parent == Kind::Description && reader.namespaceUri() == markNamespace && reader.name() == QLatin1String("layout")) {
            if (reader.attributes().hasAttribute(rdfNamespace, QLatin1String("resource"))
                || reader.attributes().hasAttribute(rdfNamespace, QLatin1String("parseType"))) {
                problems.append(QStringLiteral("the layout property is not a text value"));
                values.append(QString());
            }
            const QString text = reader.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement);
            if (reader.hasError()) {
                problems.append(QStringLiteral("the layout property is not a text value"));
                values.append(QString());
                break;
            }
            values.append(text);
        } else {
            open.append(Kind::Other);
        }
    }
    if (reader.hasError() && values.size() > before) {
        problems.append(QStringLiteral("an XMP packet with the layout property is not well-formed XML: ") + reader.errorString());
    }
}

quint64 readBig(const QByteArray &bytes, qsizetype at, int size)
{
    quint64 value = 0;
    for (int i = 0; i < size; ++i) {
        value = (value << 8) | quint8(bytes.at(at + i));
    }
    return value;
}
}

bool isJxlFile(const QByteArray &start)
{
    return start.startsWith(signature) || start.startsWith(QByteArray::fromHex("ff0a"));
}

JxlMark readJxlMark(QIODevice &device)
{
    JxlMark mark;
    if (!device.isOpen() || !device.seek(0) || device.read(signature.size()) != signature) {
        return mark;
    }
    QStringList values;
    QStringList problems;
    const qint64 end = device.size();
    qint64 position = signature.size();
    // The top-level boxes, by their sizes (ISO/IEC 18181-2:2026 5): 1 means a 64-bit size follows, 0 the rest of the file.
    while (position + 8 <= end && device.seek(position)) {
        const QByteArray head = device.read(16);
        if (head.size() < 8) {
            break;
        }
        quint64 size = readBig(head, 0, 4);
        const QByteArray type = head.mid(4, 4);
        qint64 headSize = 8;
        if (size == 1) {
            if (head.size() < 16) {
                break;
            }
            size = readBig(head, 8, 8);
            headSize = 16;
        } else if (size == 0) {
            size = quint64(end - position);
        }
        if (size < quint64(headSize) || size > quint64(end - position)) {
            break;
        }
        const qint64 payload = qint64(size) - headSize;
        if ((type == "xml " || type == "brob") && payload <= packetLimit && device.seek(position + headSize)) {
            const QByteArray content = device.read(payload);
            if (type == "xml ") {
                scanPacket(content, values, problems);
            } else if (content.size() >= 4 && content.first(4) == "xml ") {
                // 9.7: a Brotli-compressed box keeps the type of the box it holds, then its contents compressed.
                const std::optional<QByteArray> packet = inflateBrotli(content.sliced(4));
                if (packet) {
                    scanPacket(*packet, values, problems);
                } else {
                    // What it holds cannot be known, so the picture is not guessed from its shape either.
                    problems.append(QStringLiteral("a Brotli-compressed XML box cannot be decompressed"));
                    values.append(QString());
                }
            }
        }
        position += qint64(size);
    }
    if (values.isEmpty()) {
        return mark;
    }
    mark.state = JxlMark::State::Invalid;
    if (values.size() > 1) {
        mark.reason = QStringLiteral("the layout is declared %1 times").arg(values.size());
    } else if (!problems.isEmpty()) {
        mark.reason = problems.join(QLatin1String("; "));
    } else if (values.first() != markValue) {
        mark.reason = QStringLiteral("the layout %1 is not sideBySideLeftFirst, the one value of the mark").arg(values.first());
    } else {
        mark.state = JxlMark::State::Declared;
    }
    return mark;
}

JxlMark readJxlMark(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    return readJxlMark(buffer);
}

QString jxlStereoLayout(const JxlMark &mark, const QSize &presented, QString *reason)
{
    switch (mark.state) {
    case JxlMark::State::Absent:
        return {};
    case JxlMark::State::Invalid:
        if (reason) {
            *reason = mark.reason;
        }
        return QStringLiteral("mono");
    case JxlMark::State::Declared:
        if (presented.width() <= 0 || presented.width() % 2 != 0) {
            if (reason) {
                *reason = QStringLiteral("the presented width %1 is not a positive even number").arg(presented.width());
            }
            return QStringLiteral("mono");
        }
        return markValue;
    }
    return {};
}
}
