#include <QCoreApplication>

#include <picture.h>

#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: translationtest EXPECTED_TEXT\n";
        return 2;
    }

    const QString actual = Stereo::Picture::colorDisclaimer();
    if (actual != QString::fromUtf8(argv[1])) {
        std::cerr << "actual: " << actual.toStdString() << "\nexpected: " << argv[1] << '\n';
        return 1;
    }
    return 0;
}
