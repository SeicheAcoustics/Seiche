#include "AudioFile.h"

#include <QAudioDecoder>
#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QUrl>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace prs {

bool AudioFile::load(const QString& filepath) {
    return loadWav(filepath) || decodeWithQt(filepath);
}

// Fast path for plain PCM/float WAV; no plugin needed.
bool AudioFile::loadWav(const QString& filepath) {
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 44) return false;

    const char* ptr = data.constData();

    // Validate RIFF header
    if (std::memcmp(ptr, "RIFF", 4) != 0) return false;
    if (std::memcmp(ptr + 8, "WAVE", 4) != 0) return false;

    // Find fmt chunk
    qint64 pos = 12;
    int16_t audioFormat = 0;
    int16_t numChannels = 0;
    int32_t sr = 0;
    int16_t bitsPerSample = 0;

    while (pos < data.size() - 8) {
        char chunkId[5] = {};
        std::memcpy(chunkId, ptr + pos, 4);
        uint32_t chunkSize = *reinterpret_cast<const uint32_t*>(ptr + pos + 4);

        if (std::strcmp(chunkId, "fmt ") == 0) {
            audioFormat   = *reinterpret_cast<const int16_t*>(ptr + pos + 8);
            numChannels   = *reinterpret_cast<const int16_t*>(ptr + pos + 10);
            sr            = *reinterpret_cast<const int32_t*>(ptr + pos + 12);
            bitsPerSample = *reinterpret_cast<const int16_t*>(ptr + pos + 22);
        } else if (std::strcmp(chunkId, "data") == 0) {
            sampleRate_ = sr;
            channels_   = numChannels;

            // Streamed WAVs (e.g. ffmpeg piped output) write 0xFFFFFFFF; use the bytes actually present.
            qint64 dataBytes = std::min<qint64>(chunkSize, data.size() - pos - 8);
            int bytesPerSample = bitsPerSample / 8;
            if (bytesPerSample == 0) return false;
            qint64 totalSamples = dataBytes / bytesPerSample;
            samples_.resize(totalSamples);

            const char* samplePtr = ptr + pos + 8;

            if (bitsPerSample == 16) {
                for (int i = 0; i < totalSamples; ++i) {
                    int16_t val = *reinterpret_cast<const int16_t*>(samplePtr + i * 2);
                    samples_[i] = val / 32768.0f;
                }
            } else if (bitsPerSample == 32 && audioFormat == 1) {
                for (int i = 0; i < totalSamples; ++i) {
                    int32_t val = *reinterpret_cast<const int32_t*>(samplePtr + i * 4);
                    samples_[i] = val / 2147483648.0f;
                }
            } else if (bitsPerSample == 32 && audioFormat == 3) {
                for (int i = 0; i < totalSamples; ++i) {
                    samples_[i] = *reinterpret_cast<const float*>(samplePtr + i * 4);
                }
            } else {
                return false;
            }

            if (channels_ > 1) convertToMono();
            return true;
        }

        pos += 8 + qint64(chunkSize);
        if (chunkSize % 2 != 0) ++pos;
    }

    return false;
}

// MP3/FLAC/OGG and WAV encodings loadWav() rejects (e.g. 24-bit) go through Qt's FFmpeg backend.
bool AudioFile::decodeWithQt(const QString& filepath) {
    samples_.clear();
    QAudioDecoder decoder;
    if (!decoder.isSupported()) return false;

    bool ok = true, done = false;
    QEventLoop loop;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, [&] {
        const QAudioBuffer buf = decoder.read();
        const QAudioFormat fmt = buf.format();
        sampleRate_ = fmt.sampleRate();
        channels_   = fmt.channelCount();
        const char* p = buf.constData<char>();
        for (qsizetype i = 0; i < buf.sampleCount(); ++i)
            samples_.push_back(fmt.normalizedSampleValue(p + i * fmt.bytesPerSample()));
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, [&] { done = true; loop.quit(); });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), [&] {
        ok = false;
        done = true;
        loop.quit();
    });
    decoder.setSource(QUrl::fromLocalFile(filepath));
    decoder.start();
    // A bad source can report its error synchronously, before the loop would start
    if (!done) loop.exec();

    if (!ok || samples_.empty()) return false;
    if (channels_ > 1) convertToMono();
    return true;
}

// 16-bit PCM WAV; `interleaved` holds channels * frames samples in [-1, 1].
static bool writeWav16(const QString& filepath, int sampleRate, int16_t channels,
                       const std::vector<float>& interleaved) {
    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    const int dataSize = static_cast<int>(interleaved.size()) * 2;

    struct {
        char     riff[4]       = {'R','I','F','F'};
        int32_t  fileSize      = 0;
        char     wave[4]       = {'W','A','V','E'};
        char     fmt[4]        = {'f','m','t',' '};
        int32_t  fmtSize       = 16;
        int16_t  audioFormat   = 1;
        int16_t  numChannels   = 1;
        int32_t  sampleRate    = 0;
        int32_t  byteRate      = 0;
        int16_t  blockAlign    = 2;
        int16_t  bitsPerSample = 16;
        char     data[4]       = {'d','a','t','a'};
        int32_t  dataSize      = 0;
    } header;

    header.fileSize    = 36 + dataSize;
    header.numChannels = channels;
    header.sampleRate  = sampleRate;
    header.byteRate    = sampleRate * 2 * channels;
    header.blockAlign  = static_cast<int16_t>(2 * channels);
    header.dataSize    = dataSize;

    std::vector<int16_t> pcm(interleaved.size());
    for (size_t i = 0; i < interleaved.size(); ++i)
        pcm[i] = static_cast<int16_t>(std::clamp(interleaved[i], -1.0f, 1.0f) * 32767.0f);

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(pcm.data()), dataSize);
    file.close();
    return true;
}

bool AudioFile::save(const QString& filepath, int sampleRate) const {
    return writeWav16(filepath, sampleRate, 1, samples_);
}

bool AudioFile::saveStereo(const QString& filepath, int sampleRate,
                          const std::vector<float>& left,
                          const std::vector<float>& right)
{
    if (left.size() != right.size() || left.empty()) return false;
    std::vector<float> interleaved(left.size() * 2);
    for (size_t i = 0; i < left.size(); ++i) {
        interleaved[2 * i]     = left[i];
        interleaved[2 * i + 1] = right[i];
    }
    return writeWav16(filepath, sampleRate, 2, interleaved);
}

void AudioFile::convertToMono() {
    if (channels_ <= 1) return;

    int frames = static_cast<int>(samples_.size()) / channels_;
    std::vector<float> mono(frames);
    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < channels_; ++c)
            sum += samples_[i * channels_ + c];
        mono[i] = sum / channels_;
    }
    samples_ = std::move(mono);
    channels_ = 1;
}

void AudioFile::applyVolume(float volume) {
    for (auto& s : samples_)
        s *= volume;
}

} // namespace prs
