#include "RenderExports.h"

#include "audio/AudioFile.h"

#include <QFile>
#include <QJsonDocument>

namespace prs::RenderExports {

namespace {

QString csvEscape(const QString& value) {
    QString out = value;
    out.replace('"', "\"\"");
    if (out.contains(',') || out.contains('"') || out.contains('\n'))
        return '"' + out + '"';
    return out;
}

QString metricString(const QJsonObject& pair, const char* section, const char* key) {
    if (!pair.contains(section) || !pair.value(section).isObject())
        return {};
    const QJsonObject obj = pair.value(section).toObject();
    if (!obj.contains(key))
        return {};
    return QString::number(obj.value(key).toDouble(), 'f', 6);
}

} // namespace

bool saveMonoWav(const QString& path, int sampleRate, const std::vector<float>& samples) {
    AudioFile file;
    file.samples() = samples;
    return file.save(path, sampleRate);
}

bool saveStereoWav(const QString& path, int sampleRate, const std::vector<float>& left,
                   const std::vector<float>& right) {
    return AudioFile::saveStereo(path, sampleRate, left, right);
}

QJsonObject buildMetricsSummary(const QJsonArray& pairs, int sampleRate) {
    QJsonObject summary;
    summary["version"] = "1.0.0";
    summary["sample_rate"] = sampleRate;
    summary["pair_count"] = pairs.size();
    return summary;
}

bool saveJsonObject(const QString& path, const QJsonObject& obj) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

bool saveMetricsCsv(const QString& path, const QJsonArray& pairs) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QByteArray contents;
    contents += "source,listener,t20,t30,edt,direct_dB,reflected_dB,total_dB,total_dBA,total_dBB,total_dBC\n";
    for (const auto& pairVal : pairs) {
        if (!pairVal.isObject())
            continue;
        const QJsonObject pair = pairVal.toObject();
        const QString source = csvEscape(pair.value("source").toString());
        const QString listener = csvEscape(pair.value("listener").toString());
        const QString t20 = metricString(pair, "reverberation_time", "T20");
        const QString t30 = metricString(pair, "reverberation_time", "T30");
        const QString edt = metricString(pair, "reverberation_time", "EDT");
        const QString direct = metricString(pair, "sound_pressure_level", "direct_dB");
        const QString reflected = metricString(pair, "sound_pressure_level", "reflected_dB");
        const QString total = metricString(pair, "sound_pressure_level", "total_dB");
        const QString totalA = metricString(pair, "sound_pressure_level", "total_dBA");
        const QString totalB = metricString(pair, "sound_pressure_level", "total_dBB");
        const QString totalC = metricString(pair, "sound_pressure_level", "total_dBC");

        contents += source.toUtf8();
        contents += ',';
        contents += listener.toUtf8();
        contents += ',';
        contents += t20.toUtf8();
        contents += ',';
        contents += t30.toUtf8();
        contents += ',';
        contents += edt.toUtf8();
        contents += ',';
        contents += direct.toUtf8();
        contents += ',';
        contents += reflected.toUtf8();
        contents += ',';
        contents += total.toUtf8();
        contents += ',';
        contents += totalA.toUtf8();
        contents += ',';
        contents += totalB.toUtf8();
        contents += ',';
        contents += totalC.toUtf8();
        contents += '\n';
    }

    return file.write(contents) == contents.size();
}

} // namespace prs::RenderExports
