#pragma once

#include <QString>
#include <vector>

namespace prs {

class AudioFile {
public:
    AudioFile() = default;

    bool load(const QString& filepath);
    bool save(const QString& filepath, int sampleRate) const;
    /** Write stereo WAV (interleaved L,R). left and right must have the same size. */
    static bool saveStereo(const QString& filepath, int sampleRate,
                          const std::vector<float>& left,
                          const std::vector<float>& right);

    const std::vector<float>& samples() const { return samples_; }
    std::vector<float>& samples() { return samples_; }
    int sampleRate() const { return sampleRate_; }
    int channelCount() const { return channels_; }
    bool isLoaded() const { return !samples_.empty(); }

    void convertToMono();
    void applyVolume(float volume);

private:
    bool loadWav(const QString& filepath);
    bool decodeWithQt(const QString& filepath);

    std::vector<float> samples_;
    int sampleRate_ = 44100;
    int channels_   = 1;
};

} // namespace prs
