// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>

#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

class QIODevice;

namespace Stereo
{
// The JXLStereo mark (https://github.com/Sparky-OS/jxlstereo): the simple XMP property layout in that namespace, on the
// file-level description of an XMP packet that a JPEG XL file keeps in a top-level 'xml ' box, or in a 'brob' box that
// wraps one (ISO/IEC 18181-2:2026 5, 8, 9.6 and 9.7; ISO 16684-1:2019 7.4, 7.5 and 7.9.2.2, attribute and element form).
// Every such box of the file is read; exactly one declaration with the one value, sideBySideLeftFirst, is a mark. A file
// without the container (a bare codestream) has no box to carry it.
struct JxlMark {
    enum class State { Absent, Declared, Invalid };
    State state = State::Absent;
    // Why a declaration that is there is not taken.
    QString reason;
};

bool isJxlFile(const QByteArray &start);
JxlMark readJxlMark(QIODevice &device);
JxlMark readJxlMark(const QByteArray &bytes);
// The Stereo3DLayout word for a JPEG XL picture whose presented image (its orientation applied, ISO/IEC 18181-1:2024
// D.3.2) is presented: sideBySideLeftFirst for a mark on an image of positive, even width; mono, one picture as authored,
// for a declaration that is not a mark or an odd width; empty when the file declares nothing.
QString jxlStereoLayout(const JxlMark &mark, const QSize &presented, QString *reason = nullptr);
}
