#pragma once
#include "core/DocumentTypes.h"
#include <QObject>
#include <QPointer>
#include <QSet>

class QFileSystemWatcher;
class QTimer;
namespace litecode::ui {
class EditorArea;
// Model lifetime, not tab lifetime, owns these watches. All mutable state is
// confined to the workbench thread; disk reads carry immutable snapshots.
class DetachedDocumentWatcher final : public QObject {
  public:
    explicit DetachedDocumentWatcher(EditorArea* editors, QObject* parent = nullptr);
  private:
    void refresh();
    void changed(const QString& path, bool directory);
    void schedule(core::DocumentId id);
    void flush();
    void reload(core::DocumentId id);
    QPointer<EditorArea> editors_;
    QFileSystemWatcher* watcher_{};
    QTimer* timer_{};
    QSet<quint64> pending_, running_, repeat_;
};
} // namespace litecode::ui
