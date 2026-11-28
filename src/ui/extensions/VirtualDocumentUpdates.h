#pragma once
#include "extensions/WorkbenchContentProviders.h"
#include <QPointer>
namespace litecode::ui {
class EditorArea;
class VirtualDocumentUpdates final : public QObject {
  public:
    VirtualDocumentUpdates(EditorArea* editors, extensions::WorkbenchContentProviders* providers,
                           QObject* parent = nullptr);

  private:
    void update(const QUrl& uri, const QString& content);
    QPointer<EditorArea> editors_;
    QHash<quint64, quint64> generations_;
    quint64 nextGeneration_{};
};
} // namespace litecode::ui
