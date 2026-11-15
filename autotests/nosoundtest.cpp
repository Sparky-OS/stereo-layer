#include <QCoreApplication>

#include <nosound.h>

#include <iostream>

// nosoundtest EXPECTED_SENTENCE: the sentence in the language the test sets, and the one rule of when it is said
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2) {
        std::cerr << "usage: nosoundtest EXPECTED_SENTENCE\n";
        return 2;
    }
    int failures = 0;
    const QString actual = Stereo::NoSound::sentence();
    if (actual != QString::fromUtf8(argv[1])) {
        std::cerr << "actual: " << actual.toStdString() << "\nexpected: " << argv[1] << '\n';
        ++failures;
    }
    if (Stereo::NoSound().text() != actual) {
        std::cerr << "the QML text differs from the sentence\n";
        ++failures;
    }
    struct Row {
        bool loaded, video;
        int audio;
        bool says;
        const char *why;
    };
    const Row rows[] = {
        {true, true, 0, true, "a loaded film with a picture and no audio track"},
        {true, true, 1, false, "a film with sound: the control, nothing is said"},
        {true, true, 2, false, "a film with a second audio track"},
        {true, false, 1, false, "an audio file"},
        {true, false, 0, false, "a file with no track at all: nothing about a picture is known"},
        {false, true, 0, false, "tracks not read yet: nothing is said while the film loads"},
    };
    for (const Row &row : rows) {
        if (Stereo::NoSound::applies(row.loaded, row.video, row.audio) != row.says) {
            std::cerr << "rule wrong for " << row.why << '\n';
            ++failures;
        }
    }
    return failures ? 1 : 0;
}
