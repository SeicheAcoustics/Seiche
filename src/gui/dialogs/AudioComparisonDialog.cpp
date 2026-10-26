#include "AudioComparisonDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace prs {

AudioComparisonDialog::AudioComparisonDialog(const QString& outputDir, QWidget* parent)
    : QDialog(parent), outputDir_(outputDir)
{
    setWindowTitle("Compare Simulation Audio");
    setMinimumSize(600, 600);
    resize(700, 700);

    player_ = new QMediaPlayer(this);
    audioOutput_ = new QAudioOutput(this);
    player_->setAudioOutput(audioOutput_);
    audioOutput_->setVolume(0.8f);

    auto* mainLayout = new QVBoxLayout(this);

    // File list
    auto* filesGroup = new QGroupBox("Simulation Output Files");
    auto* filesLayout = new QVBoxLayout(filesGroup);
    fileList_ = new QListWidget;
    fileList_->setSelectionMode(QAbstractItemView::SingleSelection);
    filesLayout->addWidget(fileList_);
    mainLayout->addWidget(filesGroup);

    connect(fileList_, &QListWidget::itemDoubleClicked, [this](QListWidgetItem* item) {
        onPlayFile(fileList_->row(item));
    });

    // Now playing + seek
    auto* playbackGroup = new QGroupBox("Playback");
    auto* playbackLayout = new QVBoxLayout(playbackGroup);

    nowPlayingLabel_ = new QLabel("No file playing");
    playbackLayout->addWidget(nowPlayingLabel_);

    auto* seekRow = new QHBoxLayout;
    seekSlider_ = new QSlider(Qt::Horizontal);
    seekSlider_->setRange(0, 0);
    timeLabel_ = new QLabel("0:00 / 0:00");
    timeLabel_->setFixedWidth(100);
    seekRow->addWidget(seekSlider_);
    seekRow->addWidget(timeLabel_);
    playbackLayout->addLayout(seekRow);

    auto* volRow = new QHBoxLayout;
    volRow->addWidget(new QLabel("Volume:"));
    volumeSlider_ = new QSlider(Qt::Horizontal);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(80);
    stopBtn_ = new QPushButton("Stop");
    volRow->addWidget(volumeSlider_);
    volRow->addWidget(stopBtn_);
    playbackLayout->addLayout(volRow);

    mainLayout->addWidget(playbackGroup);

    // A/B comparison
    auto* abGroup = new QGroupBox("A/B Comparison");
    auto* abLayout = new QVBoxLayout(abGroup);

    auto* comboRow = new QHBoxLayout;
    comboRow->addWidget(new QLabel("A:"));
    comboA_ = new QComboBox;
    comboRow->addWidget(comboA_, 1);
    comboRow->addWidget(new QLabel("B:"));
    comboB_ = new QComboBox;
    comboRow->addWidget(comboB_, 1);
    abLayout->addLayout(comboRow);

    auto* abBtnRow = new QHBoxLayout;
    playABtn_ = new QPushButton("Play A");
    playBBtn_ = new QPushButton("Play B");
    toggleBtn_ = new QPushButton("Toggle A/B");
    abBtnRow->addWidget(playABtn_);
    abBtnRow->addWidget(playBBtn_);
    abBtnRow->addWidget(toggleBtn_);
    abLayout->addLayout(abBtnRow);

    mainLayout->addWidget(abGroup);

    // Acoustic metrics
    auto* metricsGroup = new QGroupBox("Acoustic Metrics");
    auto* metricsLayout = new QVBoxLayout(metricsGroup);
    metricsTable_ = new QTableWidget;
    metricsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    metricsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    metricsTable_->setAlternatingRowColors(true);
    metricsTable_->verticalHeader()->setVisible(false);
    metricsTable_->horizontalHeader()->setStretchLastSection(true);
    metricsLayout->addWidget(metricsTable_);
    mainLayout->addWidget(metricsGroup);

    // Close
    auto* closeBtn = new QPushButton("Close");
    mainLayout->addWidget(closeBtn);

    // Connections
    connect(seekSlider_, &QSlider::sliderMoved, this, &AudioComparisonDialog::onSeek);
    connect(volumeSlider_, &QSlider::valueChanged, this, &AudioComparisonDialog::onVolumeChanged);
    connect(stopBtn_, &QPushButton::clicked, this, &AudioComparisonDialog::onStopPlayback);
    connect(playABtn_, &QPushButton::clicked, this, &AudioComparisonDialog::onPlayA);
    connect(playBBtn_, &QPushButton::clicked, this, &AudioComparisonDialog::onPlayB);
    connect(toggleBtn_, &QPushButton::clicked, this, &AudioComparisonDialog::onToggleAB);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(player_, &QMediaPlayer::positionChanged, this, &AudioComparisonDialog::onPositionChanged);
    connect(player_, &QMediaPlayer::durationChanged, this, &AudioComparisonDialog::onDurationChanged);
    connect(player_, &QMediaPlayer::playbackStateChanged, this, &AudioComparisonDialog::onPlayerStateChanged);

    populateFileList(outputDir);
    loadMetrics(outputDir);
}

AudioComparisonDialog::~AudioComparisonDialog() {
    player_->stop();
}

void AudioComparisonDialog::populateFileList(const QString& dir) {
    QDir d(dir);
    QStringList filters = {"*.wav"};
    QStringList files = d.entryList(filters, QDir::Files, QDir::Name);

    wavFiles_.clear();
    fileList_->clear();
    comboA_->clear();
    comboB_->clear();

    for (const auto& f : files) {
        QString fullPath = d.absoluteFilePath(f);
        wavFiles_ << fullPath;
        fileList_->addItem(f);
        comboA_->addItem(f);
        comboB_->addItem(f);
    }

    if (comboB_->count() > 1)
        comboB_->setCurrentIndex(1);
}

void AudioComparisonDialog::loadMetrics(const QString& dir) {
    QFile file(QDir(dir).filePath("metrics.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    QJsonArray pairs = doc.object()["pairs"].toArray();
    if (pairs.isEmpty())
        return;

    metricsTable_->setColumnCount(11);
    metricsTable_->setHorizontalHeaderLabels({
        "Source", "Listener",
        "T20 (s)", "T30 (s)", "EDT (s)",
        "Direct (dB)", "Reflected (dB)", "Total (dB)",
        "Total dB(A)", "Total dB(B)", "Total dB(C)"
    });
    metricsTable_->setRowCount(pairs.size());

    for (int row = 0; row < pairs.size(); ++row) {
        QJsonObject pair = pairs[row].toObject();

        metricsTable_->setItem(row, 0, new QTableWidgetItem(pair["source"].toString()));
        metricsTable_->setItem(row, 1, new QTableWidgetItem(pair["listener"].toString()));

        QJsonObject rt = pair["reverberation_time"].toObject();
        QJsonObject spl = pair["sound_pressure_level"].toObject();

        auto fmtVal = [](const QJsonObject& obj, const QString& key) -> QString {
            if (!obj.contains(key)) return "—";
            return QString::number(obj[key].toDouble(), 'f', 3);
        };

        metricsTable_->setItem(row, 2, new QTableWidgetItem(fmtVal(rt, "T20")));
        metricsTable_->setItem(row, 3, new QTableWidgetItem(fmtVal(rt, "T30")));
        metricsTable_->setItem(row, 4, new QTableWidgetItem(fmtVal(rt, "EDT")));
        metricsTable_->setItem(row, 5, new QTableWidgetItem(fmtVal(spl, "direct_dB")));
        metricsTable_->setItem(row, 6, new QTableWidgetItem(fmtVal(spl, "reflected_dB")));
        metricsTable_->setItem(row, 7, new QTableWidgetItem(fmtVal(spl, "total_dB")));
        metricsTable_->setItem(row, 8, new QTableWidgetItem(fmtVal(spl, "total_dBA")));
        metricsTable_->setItem(row, 9, new QTableWidgetItem(fmtVal(spl, "total_dBB")));
        metricsTable_->setItem(row, 10, new QTableWidgetItem(fmtVal(spl, "total_dBC")));
    }

    metricsTable_->resizeColumnsToContents();
}

void AudioComparisonDialog::playFile(const QString& path) {
    player_->stop();
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
    nowPlayingLabel_->setText("Playing: " + QFileInfo(path).fileName());
}

void AudioComparisonDialog::onPlayFile(int row) {
    if (row >= 0 && row < wavFiles_.size())
        playFile(wavFiles_[row]);
}

void AudioComparisonDialog::onStopPlayback() {
    player_->stop();
    nowPlayingLabel_->setText("Stopped");
}

void AudioComparisonDialog::onPlayA() {
    int idx = comboA_->currentIndex();
    if (idx >= 0 && idx < wavFiles_.size()) {
        playFile(wavFiles_[idx]);
        playingA_ = true;
    }
}

void AudioComparisonDialog::onPlayB() {
    int idx = comboB_->currentIndex();
    if (idx >= 0 && idx < wavFiles_.size()) {
        playFile(wavFiles_[idx]);
        playingA_ = false;
    }
}

void AudioComparisonDialog::onToggleAB() {
    if (playingA_)
        onPlayB();
    else
        onPlayA();
}

void AudioComparisonDialog::onPositionChanged(qint64 position) {
    if (!seekSlider_->isSliderDown())
        seekSlider_->setValue(static_cast<int>(position));
    timeLabel_->setText(formatTime(position) + " / " + formatTime(player_->duration()));
}

void AudioComparisonDialog::onDurationChanged(qint64 duration) {
    seekSlider_->setRange(0, static_cast<int>(duration));
}

void AudioComparisonDialog::onSeek(int position) {
    player_->setPosition(position);
}

void AudioComparisonDialog::onVolumeChanged(int value) {
    audioOutput_->setVolume(value / 100.0f);
}

void AudioComparisonDialog::onPlayerStateChanged(QMediaPlayer::PlaybackState state) {
    if (state == QMediaPlayer::StoppedState) {
        nowPlayingLabel_->setText("Stopped");
    }
}

QString AudioComparisonDialog::formatTime(qint64 ms) const {
    int secs = static_cast<int>(ms / 1000);
    int mins = secs / 60;
    secs %= 60;
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QChar('0'));
}

} // namespace prs
