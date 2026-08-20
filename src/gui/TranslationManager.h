#pragma once

#include <QObject>
#include <QString>

class QAction;

namespace prs {

class TranslationManager final : public QObject {
    Q_OBJECT

public:
    static TranslationManager& instance();

    void initialize();
    QString language() const { return language_; }
    bool isChinese() const { return language_ == QStringLiteral("zh_CN"); }
    void setLanguage(const QString& language);
    QString translate(const QString& source) const;
    void translateApplication();

signals:
    void languageChanged(const QString& language);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    TranslationManager();
    void translateObject(QObject* object);
    void translateAction(QAction* action);
    QString language_ = QStringLiteral("zh_CN");
};

} // namespace prs
