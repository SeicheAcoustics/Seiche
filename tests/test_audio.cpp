#include <QtTest/QtTest>
#include <QTemporaryFile>
#include "audio/AudioFile.h"
#include "audio/SignalProcessing.h"

using namespace prs;

class TestAudio : public QObject {
    Q_OBJECT
private slots:
    void testAudioFileSaveAndLoad() {
        AudioFile af;
        std::vector<float> samples(1000);
        for (int i = 0; i < 1000; ++i)
            samples[i] = static_cast<float>(i) / 1000.0f;
        af.samples() = samples;

        QTemporaryFile tmp;
        tmp.setAutoRemove(true);
        tmp.setFileTemplate(QDir::tempPath() + "/testXXXXXX.wav");
        QVERIFY(tmp.open());
        QString path = tmp.fileName();
        tmp.close();

        QVERIFY(af.save(path, 44100));

        AudioFile loaded;
        QVERIFY(loaded.load(path));
        QCOMPARE(loaded.sampleRate(), 44100);
        QVERIFY(loaded.samples().size() == 1000);
    }

    void testLoadStreamedWav() {
        AudioFile af;
        af.samples().assign(1000, 0.25f);

        QTemporaryFile tmp;
        tmp.setFileTemplate(QDir::tempPath() + "/testXXXXXX.wav");
        QVERIFY(tmp.open());
        QString path = tmp.fileName();
        tmp.close();
        QVERIFY(af.save(path, 44100));

        // Streamed writers leave the RIFF and data sizes as 0xFFFFFFFF
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        const char unknown[4] = {'\xff', '\xff', '\xff', '\xff'};
        f.seek(4);  f.write(unknown, 4);
        f.seek(40); f.write(unknown, 4);
        f.close();

        AudioFile loaded;
        QVERIFY(loaded.load(path));
        QCOMPARE(loaded.samples().size(), size_t(1000));
    }

    void testLoadDecodesViaQt() {
        // 24-bit PCM: rejected by the built-in WAV parser, so this exercises the QAudioDecoder path
        const int frames = 480;
        QByteArray wav;
        QDataStream out(&wav, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4); out << quint32(36 + frames * 3);
        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1)
            << quint32(48000) << quint32(48000 * 3) << quint16(3) << quint16(24);
        out.writeRawData("data", 4); out << quint32(frames * 3);
        for (int i = 0; i < frames; ++i) out << quint8(0) << quint8(0) << quint8(0x40);  // +0.5

        QTemporaryFile tmp(QDir::tempPath() + "/testXXXXXX.wav");
        QVERIFY(tmp.open());
        tmp.write(wav);
        tmp.close();

        AudioFile loaded;
        QVERIFY(loaded.load(tmp.fileName()));
        QCOMPARE(loaded.sampleRate(), 48000);
        QCOMPARE(loaded.samples().size(), size_t(frames));
        QVERIFY(std::abs(loaded.samples()[frames / 2] - 0.5f) < 1e-3f);
    }

    void testLoadMissingFileFails() {
        AudioFile af;
        QVERIFY(!af.load(QDir::tempPath() + "/does_not_exist_seiche.mp3"));
    }

    void testMonoConversion() {
        AudioFile af;
        std::vector<float> stereo(200);
        for (int i = 0; i < 200; ++i)
            stereo[i] = (i % 2 == 0) ? 1.0f : 0.0f;
        af.samples() = stereo;
        af.convertToMono();
        QVERIFY(!af.samples().empty());
    }

    void testVolumeApplication() {
        AudioFile af;
        af.samples() = {0.5f, -0.5f, 1.0f};
        af.applyVolume(0.5f);
        QVERIFY(std::abs(af.samples()[0] - 0.25f) < 1e-6f);
        QVERIFY(std::abs(af.samples()[1] + 0.25f) < 1e-6f);
    }

    void testNormalize() {
        std::vector<float> signal = {0.2f, -0.5f, 0.3f};
        SignalProcessing::normalize(signal, 0.95f);
        float maxAbs = 0;
        for (float s : signal) maxAbs = (std::max)(maxAbs, std::abs(s));
        QVERIFY(std::abs(maxAbs - 0.95f) < 1e-5f);
    }

    void testConvolve() {
        std::vector<float> a = {1, 0, 0};
        std::vector<float> b = {1, 2, 3};
        auto result = SignalProcessing::convolve(a, b);
        QCOMPARE(result.size(), static_cast<size_t>(5));
        QVERIFY(std::abs(result[0] - 1.0f) < 1e-5f);
        QVERIFY(std::abs(result[1] - 2.0f) < 1e-5f);
        QVERIFY(std::abs(result[2] - 3.0f) < 1e-5f);
    }

    void testFftConvolve() {
        std::vector<float> a = {1, 0, 0, 0};
        std::vector<float> b = {1, 2, 3, 4};
        auto result = SignalProcessing::fftConvolve(a, b);
        QVERIFY(result.size() >= 4);
        QVERIFY(std::abs(result[0] - 1.0f) < 1e-4f);
        QVERIFY(std::abs(result[1] - 2.0f) < 1e-4f);
        QVERIFY(std::abs(result[2] - 3.0f) < 1e-4f);
        QVERIFY(std::abs(result[3] - 4.0f) < 1e-4f);
    }
};

QTEST_MAIN(TestAudio)
#include "test_audio.moc"
