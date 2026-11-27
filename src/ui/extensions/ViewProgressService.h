#pragma once
#include "extensions/WorkbenchProgress.h"
#include <QObject>
#include <QPointer>
#include <QHash>
#include <QSet>
class QWidget;
class QProgressBar;
namespace litecode::ui {
// Workbench-only view registry. Does not add a LiteCode extension-facing API.
class ViewProgressService final : public QObject {
    Q_OBJECT
  public:
    explicit ViewProgressService(extensions::WorkbenchProgress& progress, QObject* parent);
    ~ViewProgressService() override;
    void registerView(const QString& id, QWidget& body, QWidget* activity = nullptr);
  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
  private:
    struct View {
        QPointer<QWidget> body, activity;
        QPointer<QProgressBar> bar, badge;
        QSet<QString> tasks;
    };
    void position(View& view);
    void clear();
    QHash<QString, View> views_;
    QHash<QString, QString> taskViews_;
};
} // namespace litecode::ui
