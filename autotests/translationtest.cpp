#include <QCoreApplication>

#include <depthsettings.h>
#include <filechoice.h>
#include <picture.h>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 4) {
        std::cerr << "usage: translationtest EXPECTED_TEXT EXPECTED_DEPTH_STRENGTH_LABEL EXPECTED_ENABLE_3D\n";
        return 2;
    }

    const QString actual = Stereo::Picture::colorDisclaimer();
    if (actual != QString::fromUtf8(argv[1])) {
        std::cerr << "actual: " << actual.toStdString() << "\nexpected: " << argv[1] << '\n';
        return 1;
    }
    // the depth controls' words are translated too
    const QString label = Stereo::DepthSettings::label(QStringLiteral("strength"));
    if (label != QString::fromUtf8(argv[2])) {
        std::cerr << "actual label: " << label.toStdString() << "\nexpected: " << argv[2] << '\n';
        return 1;
    }
    // and the program's own "Enable 3D"
    const QString enable = Stereo::Enable3D().text();
    if (enable != QString::fromUtf8(argv[3])) {
        std::cerr << "actual Enable 3D: " << enable.toStdString() << "\nexpected: " << argv[3] << '\n';
        return 1;
    }
    return 0;
}
