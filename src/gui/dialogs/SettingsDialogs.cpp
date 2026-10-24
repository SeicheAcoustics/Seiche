#include "SettingsDialogs.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>

namespace prs {

// ==================== PreferencesDialog ====================

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Preferences");
    setMinimumWidth(400);
    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout;

    auto* dirRow = new QHBoxLayout;
    projectDirEdit_ = new QLineEdit;
    QSettings s("Seiche", "Seiche");
    projectDirEdit_->setText(s.value("defaultProjectDir", "").toString());
    auto* browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Default Project Directory");
        if (!dir.isEmpty()) projectDirEdit_->setText(dir);
    });
    dirRow->addWidget(projectDirEdit_);
    dirRow->addWidget(browseBtn);
    form->addRow("Default Project Dir:", dirRow);

    autoSaveInterval_ = new QSpinBox;
    autoSaveInterval_->setRange(0, 60);
    autoSaveInterval_->setSuffix(" min");
    autoSaveInterval_->setSpecialValueText("Off");
    autoSaveInterval_->setValue(s.value("autoSaveInterval", 0).toInt());
    form->addRow("Auto-save Interval:", autoSaveInterval_);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void PreferencesDialog::accept() {
    QSettings s("Seiche", "Seiche");
    s.setValue("defaultProjectDir", projectDirEdit_->text());
    s.setValue("autoSaveInterval", autoSaveInterval_->value());
    QDialog::accept();
}

// ==================== DisplaySettingsDialog ====================

DisplaySettingsDialog::DisplaySettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Display Settings");
    setMinimumWidth(350);
    auto* layout = new QVBoxLayout(this);

    QSettings s("Seiche", "Seiche");

    auto* form = new QFormLayout;

    gridVisible_ = new QCheckBox("Show measurement grid");
    gridVisible_->setChecked(s.value("display/gridVisible", true).toBool());
    form->addRow(gridVisible_);

    gridSpacing_ = new QDoubleSpinBox;
    gridSpacing_->setRange(0.1, 10.0);
    gridSpacing_->setSingleStep(0.1);
    gridSpacing_->setSuffix(" m");
    gridSpacing_->setValue(s.value("display/gridSpacing", 1.0).toDouble());
    form->addRow("Grid minor spacing:", gridSpacing_);

    auto* alphaRow = new QHBoxLayout;
    transparencyAlpha_ = new QSlider(Qt::Horizontal);
    transparencyAlpha_->setRange(5, 100);
    transparencyAlpha_->setValue(static_cast<int>(s.value("display/transparencyAlpha", 0.55).toDouble() * 100));
    alphaLabel_ = new QLabel(QString::number(transparencyAlpha_->value() / 100.0, 'f', 2));
    connect(transparencyAlpha_, &QSlider::valueChanged, [this](int v) {
        alphaLabel_->setText(QString::number(v / 100.0, 'f', 2));
    });
    alphaRow->addWidget(transparencyAlpha_);
    alphaRow->addWidget(alphaLabel_);
    form->addRow("Transparency alpha:", alphaRow);

    markerSize_ = new QSpinBox;
    markerSize_->setRange(5, 50);
    markerSize_->setValue(s.value("display/markerSize", 15).toInt());
    form->addRow("Point marker size:", markerSize_);

    solidColorsOnly_ = new QCheckBox("Solid colors only (ignore surface textures)");
    solidColorsOnly_->setChecked(!s.value("display/texturesEnabled", true).toBool());
    form->addRow(solidColorsOnly_);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void DisplaySettingsDialog::accept() {
    QSettings s("Seiche", "Seiche");
    s.setValue("display/gridVisible", gridVisible_->isChecked());
    s.setValue("display/gridSpacing", gridSpacing_->value());
    s.setValue("display/transparencyAlpha", transparencyAlpha_->value() / 100.0);
    s.setValue("display/markerSize", markerSize_->value());
    s.setValue("display/texturesEnabled", !solidColorsOnly_->isChecked());
    emit settingsChanged();
    QDialog::accept();
}

// ==================== AudioSettingsDialog ====================

AudioSettingsDialog::AudioSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Audio Settings");
    setMinimumWidth(300);
    auto* layout = new QVBoxLayout(this);

    QSettings s("Seiche", "Seiche");

    auto* form = new QFormLayout;
    sampleRate_ = new QComboBox;
    sampleRate_->addItem("22050 Hz", 22050);
    sampleRate_->addItem("44100 Hz", 44100);
    sampleRate_->addItem("48000 Hz", 48000);
    sampleRate_->addItem("96000 Hz", 96000);
    int savedRate = s.value("audio/sampleRate", 44100).toInt();
    int idx = sampleRate_->findData(savedRate);
    if (idx >= 0) sampleRate_->setCurrentIndex(idx);
    form->addRow("Sample Rate:", sampleRate_);

    auto* formatLabel = new QLabel("WAV (16-bit PCM)");
    formatLabel->setStyleSheet("font-size: 11px;");
    form->addRow("Output Format:", formatLabel);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AudioSettingsDialog::accept() {
    QSettings s("Seiche", "Seiche");
    s.setValue("audio/sampleRate", sampleRate_->currentData().toInt());
    QDialog::accept();
}

// ==================== SimulationSettingsDialog ====================

SimulationSettingsDialog::SimulationSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Simulation Settings");
    setMinimumWidth(400);
    auto* layout = new QVBoxLayout(this);

    QSettings s("Seiche", "Seiche");

    auto* form = new QFormLayout;

    maxOrder_ = new QSpinBox;
    maxOrder_->setRange(1, 10);
    maxOrder_->setValue(s.value("sim/maxOrder", 3).toInt());
    form->addRow("Max ISM Order:", maxOrder_);

    numRays_ = new QSpinBox;
    numRays_->setRange(1000, 100000);
    numRays_->setSingleStep(1000);
    numRays_->setValue(s.value("sim/numRays", 10000).toInt());
    form->addRow("Number of Rays:", numRays_);

    absorption_ = new QDoubleSpinBox;
    absorption_->setRange(0.0, 1.0);
    absorption_->setSingleStep(0.05);
    absorption_->setValue(s.value("sim/absorption", 0.2).toDouble());
    form->addRow("Default Absorption:", absorption_);

    scattering_ = new QDoubleSpinBox;
    scattering_->setRange(0.0, 1.0);
    scattering_->setSingleStep(0.05);
    scattering_->setValue(s.value("sim/scattering", 0.1).toDouble());
    form->addRow("Default Scattering:", scattering_);

    airAbsorption_ = new QCheckBox("Enable air absorption");
    airAbsorption_->setChecked(s.value("sim/airAbsorption", true).toBool());
    form->addRow(airAbsorption_);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SimulationSettingsDialog::accept() {
    QSettings s("Seiche", "Seiche");
    s.setValue("sim/maxOrder", maxOrder_->value());
    s.setValue("sim/numRays", numRays_->value());
    s.setValue("sim/absorption", absorption_->value());
    s.setValue("sim/scattering", scattering_->value());
    s.setValue("sim/airAbsorption", airAbsorption_->isChecked());
    QDialog::accept();
}

// ==================== KeyboardShortcutsDialog ====================

KeyboardShortcutsDialog::KeyboardShortcutsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Keyboard Shortcuts");
    setMinimumSize(400, 350);
    auto* layout = new QVBoxLayout(this);

    table_ = new QTableWidget;
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({"Action", "Shortcut"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);

    struct SC { const char* action; const char* shortcut; };
    SC shortcuts[] = {
        {"New Project",          "Ctrl+N"},
        {"Open Project",         "Ctrl+O"},
        {"Save Project",         "Ctrl+S"},
        {"Save As",              "Ctrl+Shift+S"},
        {"Undo",                 "Ctrl+Z"},
        {"Redo",                 "Ctrl+Y"},
        {"Cut Point",            "Ctrl+X"},
        {"Copy Point",           "Ctrl+C"},
        {"Paste Point",          "Ctrl+V"},
        {"Delete Point",         "Delete"},
        {"Placement (off / Source / Listener / off)", "P"},
        {"Toggle Transparency",  "T"},
        {"Reset Camera",         "R"},
        {"Clear All Points",     "C"},
        {"Move View",            "W/A/S/D, Arrows"},
        {"Zoom Toward Cursor",   "Mouse Wheel"},
        {"Exit",                 "Ctrl+Q"},
    };

    int numShortcuts = sizeof(shortcuts) / sizeof(shortcuts[0]);
    table_->setRowCount(numShortcuts);
    for (int i = 0; i < numShortcuts; ++i) {
        table_->setItem(i, 0, new QTableWidgetItem(shortcuts[i].action));
        table_->setItem(i, 1, new QTableWidgetItem(shortcuts[i].shortcut));
    }
    table_->resizeColumnsToContents();

    layout->addWidget(table_);

    auto* closeBtn = new QPushButton("Close");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    layout->addWidget(closeBtn);
}

} // namespace prs
