// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
// A reader of test files that behaves as the edition's readers of pictures with a depth map do (KImageFormats' HEIF,
// AVIF and JPEG plugins): Stereo3DLayout and Stereo3DDepth in its description, the depth map as the second image, the
// original picture as the third. A file is "SLDT" and a QDataStream of the layout, the depth mark and the images.
#include <QDataStream>
#include <QImage>
#include <QImageIOPlugin>
#include <QList>

class DepthTestHandler : public QImageIOHandler
{
public:
    bool canRead() const override { return m_scanned ? !m_images.isEmpty() : device()->peek(4) == "SLDT"; }
    bool read(QImage *image) override
    {
        if (!scan() || m_current >= m_images.size())
            return false;
        *image = m_images.at(m_current);
        image->setText(QStringLiteral("Stereo3DLayout"), m_layout);
        if (!m_mark.isEmpty())
            image->setText(QStringLiteral("Stereo3DDepth"), m_mark);
        return true;
    }
    int imageCount() const override { return scan() ? int(m_images.size()) : 0; }
    int currentImageNumber() const override { return m_current; }
    bool jumpToImage(int number) override
    {
        if (!scan() || number < 0 || number >= m_images.size())
            return false;
        m_current = number;
        return true;
    }
    bool jumpToNextImage() override { return jumpToImage(m_current + 1); }
    bool supportsOption(ImageOption option) const override { return option == Description || option == Size; }
    QVariant option(ImageOption option) const override
    {
        if (!scan())
            return {};
        if (option == Size)
            return m_images.value(m_current).size();
        if (option == Description) {
            QString text = QStringLiteral("Stereo3DLayout: ") + m_layout;
            if (!m_mark.isEmpty())
                text += QStringLiteral("\n\nStereo3DDepth: ") + m_mark;
            return text;
        }
        return {};
    }

private:
    bool scan() const
    {
        if (!m_scanned) {
            m_scanned = true;
            if (device()->read(4) == "SLDT") {
                QDataStream in(device());
                qint32 count = 0;
                in >> m_layout >> m_mark >> count;
                for (int i = 0; i < count && in.status() == QDataStream::Ok; ++i) {
                    QImage image;
                    in >> image;
                    m_images.append(image);
                }
            }
        }
        return !m_images.isEmpty();
    }
    mutable bool m_scanned = false;
    mutable QString m_layout;
    mutable QString m_mark;
    mutable QList<QImage> m_images;
    int m_current = 0;
};

class DepthTestPlugin : public QImageIOPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "depthreader.json")
public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override
    {
        if (format == "sldt" || (format.isEmpty() && device && device->peek(4) == "SLDT"))
            return CanRead;
        return {};
    }
    QImageIOHandler *create(QIODevice *device, const QByteArray &format) const override
    {
        auto *handler = new DepthTestHandler;
        handler->setDevice(device);
        handler->setFormat(format.isEmpty() ? QByteArray("sldt") : format);
        return handler;
    }
};

#include "depthreader.moc"
