// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#include "panorama_p.h"
#include <QXmlStreamReader>
#include <QtEndian>
namespace Stereo
{
bool hasEquirectangularPanoramaMark(const QByteArray &data)
{
    constexpr QByteArrayView xmpHeader("http://ns.adobe.com/xap/1.0/\0", 29);
    const QString panoramaNamespace = QStringLiteral("http://ns.google.com/photos/1.0/panorama/");
    if (!data.startsWith(QByteArray::fromHex("ffd8"))) {
        return false;
    }
    qsizetype offset = 2;
    while (offset + 4 <= data.size() && uchar(data[offset]) == 0xff) {
        const quint8 marker = uchar(data[offset + 1]);
        if (marker == 0xda || marker == 0xd9) {
            break;
        }
        const quint16 length = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset + 2));
        if (length < 2 || offset + 2 + length > data.size()) {
            break;
        }
        const QByteArray payload = data.mid(offset + 4, length - 2);
        if (marker == 0xe1 && QByteArrayView(payload).startsWith(xmpHeader)) {
            QXmlStreamReader xml(payload.mid(xmpHeader.size()));
            bool panorama = false;
            while (!xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement()) {
                    continue;
                }
                for (const auto &attribute : xml.attributes()) {
                    if (attribute.namespaceUri() == panoramaNamespace && attribute.name() == QLatin1String("ProjectionType")) {
                        panorama |= attribute.value() == QLatin1String("equirectangular");
                    }
                }
                if (xml.namespaceUri() == panoramaNamespace && xml.name() == QLatin1String("ProjectionType")) {
                    panorama |= xml.readElementText().trimmed() == QLatin1String("equirectangular");
                }
            }
            if (!xml.hasError() && panorama) {
                return true;
            }
        }
        offset += 2 + length;
    }
    return false;
}

}
