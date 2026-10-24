#include "LibraryPanel.h"
#include "core/MaterialLoader.h"
#include "widgets/TreeFilter.h"

#include <QLabel>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QLineEdit>
#include <QHash>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

namespace prs {

namespace {

constexpr int kDragDataRole = Qt::UserRole + 1;

QByteArray serializeMaterial(const Material& mat) {
    QJsonObject obj;
    obj["name"] = QString::fromStdString(mat.name);
    obj["category"] = QString::fromStdString(mat.category);
    obj["thickness"] = QString::fromStdString(mat.thickness);
    obj["scattering"] = static_cast<double>(mat.scattering);
    obj["texture"] = QString::fromStdString(mat.texturePath);

    QJsonObject abs;
    for (int i = 0; i < NUM_FREQ_BANDS; ++i)
        abs[QString::number(FREQ_BANDS[i])] = static_cast<double>(mat.absorption[i]);
    obj["absorption"] = abs;

    QJsonArray c;
    c.append(mat.color[0]);
    c.append(mat.color[1]);
    c.append(mat.color[2]);
    obj["color"] = c;

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// Leaves carry their serialized material so they can be dropped onto the viewport.
class MaterialTree : public QTreeWidget {
protected:
    QStringList mimeTypes() const override { return {"application/x-prs-material"}; }
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override {
        if (items.isEmpty() || !items.first()->data(0, kDragDataRole).isValid())
            return nullptr;
        auto* mime = new QMimeData;
        mime->setData("application/x-prs-material", items.first()->data(0, kDragDataRole).toByteArray());
        return mime;
    }
};

} // namespace

LibraryPanel::LibraryPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void LibraryPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    auto* header = new QLabel("LIBRARY");
    header->setAlignment(Qt::AlignCenter);
    header->setFixedHeight(QFontMetrics(header->font()).height() + 8);
    header->setStyleSheet("background: #e0e0e0; font-weight: bold; padding: 0 4px;");
    layout->addWidget(header);

    search_ = new QLineEdit;
    search_->setPlaceholderText("Search sounds and materials...");
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, &LibraryPanel::applyFilter);
    layout->addWidget(search_);

    tabWidget_ = new QTabWidget;
    tabWidget_->setTabPosition(QTabWidget::North);

    auto* soundTab    = new QWidget;
    auto* materialTab = new QWidget;

    // Material tab first: the sound tab's startup scan calls applyFilter(), which reads materialTree_.
    createMaterialTab(materialTab);
    createSoundTab(soundTab);

    tabWidget_->addTab(soundTab, "SOUND");
    tabWidget_->addTab(materialTab, "MATERIAL");

    layout->addWidget(tabWidget_);
}

void LibraryPanel::createSoundTab(QWidget* tab) {
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    soundDirLabel_ = new QLabel("No folder selected");
    soundDirLabel_->setStyleSheet("font-size: 10px;");
    soundDirLabel_->setWordWrap(true);
    layout->addWidget(soundDirLabel_);

    auto* browseBtn = new QPushButton("Browse Folder...");
    connect(browseBtn, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Sound Folder");
        if (!dir.isEmpty()) {
            scanSoundDirectory(dir);
            QSettings s("Seiche", "Seiche");
            s.setValue("soundDirectory", dir);
        }
    });
    layout->addWidget(browseBtn);

    soundList_ = new QListWidget;
    soundList_->setAlternatingRowColors(true);
    soundList_->setWordWrap(true);
    soundList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(soundList_, &QListWidget::itemDoubleClicked, [this](QListWidgetItem* item) {
        QString filepath = item->data(Qt::UserRole).toString();
        emit soundFileSelected(filepath);
    });
    layout->addWidget(soundList_);

    auto* hintLabel = new QLabel("Double-click to select a sound file");
    hintLabel->setStyleSheet("font-size: 9px;");
    hintLabel->setAlignment(Qt::AlignCenter);
    hintLabel->setWordWrap(true);
    layout->addWidget(hintLabel);

    QSettings s("Seiche", "Seiche");
    QString savedDir = s.value("soundDirectory", "sounds/sources").toString();
    if (QDir(savedDir).exists())
        scanSoundDirectory(savedDir);
}

void LibraryPanel::scanSoundDirectory(const QString& dir) {
    soundDirectory_ = dir;
    soundDirLabel_->setText(QDir(dir).dirName());
    soundDirLabel_->setToolTip(dir);

    soundList_->clear();

    QDir directory(dir);
    QStringList filters = {"*.wav", "*.mp3", "*.flac", "*.ogg"};
    QFileInfoList files = directory.entryInfoList(filters, QDir::Files, QDir::Name);

    for (auto& fi : files) {
        auto* item = new QListWidgetItem(fi.fileName());
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        item->setToolTip(fi.absoluteFilePath());
        soundList_->addItem(item);
    }
    applyFilter();
}

void LibraryPanel::applyFilter() {
    const QString text = search_->text();
    for (int i = 0; i < soundList_->count(); ++i) {
        auto* item = soundList_->item(i);
        item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
    }
    for (int i = 0; i < materialTree_->topLevelItemCount(); ++i)
        filterTreeItem(materialTree_->topLevelItem(i), text);
}

void LibraryPanel::createMaterialTab(QWidget* tab) {
    materialTree_ = new MaterialTree;
    materialTree_->setHeaderHidden(true);
    materialTree_->setColumnCount(1);
    materialTree_->setDragEnabled(true);
    materialTree_->setDragDropMode(QAbstractItemView::DragOnly);
    materialTree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    materialTree_->setTextElideMode(Qt::ElideRight);

    // Load materials from the materials/ directory next to the executable
    QString materialsDir = QCoreApplication::applicationDirPath() + "/materials";
    if (!QDir(materialsDir).exists()) {
        // Fall back to source tree path for development
        materialsDir = "materials";
    }

    auto categories = MaterialLoader::loadFromDirectory(materialsDir);

    // Category names are paths ("Wall Treatments/Curtains Fabrics"); build one group per segment.
    QHash<QString, QTreeWidgetItem*> groups;
    std::function<QTreeWidgetItem*(const QString&)> groupFor = [&](const QString& path) {
        if (auto* g = groups.value(path)) return g;
        int slash = path.lastIndexOf('/');
        QTreeWidgetItem* g = slash < 0
            ? new QTreeWidgetItem(materialTree_)
            : new QTreeWidgetItem(groupFor(path.left(slash)));
        g->setText(0, path.mid(slash + 1));
        g->setFlags(Qt::ItemIsEnabled);
        QFont bold = g->font(0);
        bold.setBold(true);
        g->setFont(0, bold);
        groups.insert(path, g);
        return g;
    };

    for (auto& category : categories) {
        auto* group = groupFor(QString::fromStdString(category.name));
        for (auto& mat : category.materials) {
            auto* leaf = new QTreeWidgetItem(group);
            leaf->setText(0, QString::fromStdString(mat.name));
            leaf->setToolTip(0, leaf->text(0));
            leaf->setData(0, Qt::UserRole, static_cast<int>(materials_.size()));
            leaf->setData(0, kDragDataRole, serializeMaterial(mat));
            materials_.push_back(mat);
        }
    }

    connect(materialTree_, &QTreeWidget::itemClicked, this, [](QTreeWidgetItem* item) {
        if (!item->data(0, Qt::UserRole).isValid())
            item->setExpanded(!item->isExpanded());
    });
    connect(materialTree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        bool ok = false;
        int idx = item->data(0, Qt::UserRole).toInt(&ok);
        if (ok)
            emit materialSelected(materials_[idx]);
    });

    auto* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->addWidget(materialTree_);
}

} // namespace prs
