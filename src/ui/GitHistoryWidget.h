#pragma once
#include "ui/components/SidebarView.h"
#include "workspace/GitService.h"
#include <QPointer>
#include <QQueue>
#include <QWidget>
#include <functional>
class QTreeWidget;
class QLabel;
class QPushButton;
namespace litecode::ui {
class GitHistoryWidget final : public components::SidebarView {
    Q_OBJECT
  public:
    explicit GitHistoryWidget(QWidget* parent = nullptr);
    void setWorkspace(const QString& path);
    void reload();
    void setFilter(const QStringList& references, bool automatic);
    void chooseFilter(QWidget* anchor);
    void revealCurrentCommit();
    void navigateCommit(int direction);
    QString filterLabel() const;
  signals:
    void comparisonRequested(const litecode::workspace::GitComparison& comparison);
    void filterChanged();

  private:
    void schedule(std::function<void()> action);
    void render(const QVector<workspace::GitCommit>& commits);
    workspace::GitService* git_{};
    QTreeWidget* tree_{};
    QLabel* notice_{};
    QPushButton* more_{};
    QQueue<std::function<void()>> queue_;
    int limit_{100};
    QStringList references_;
    bool automatic_{true};
    QPointer<QWidget> filterAnchor_;
    void loadHistory();
    QVector<workspace::GitCommit> commits_;
    workspace::GitDivergence divergence_;
    quint64 generation_{}, requestedGeneration_{};
    bool waitingForDivergence_{};
};
} // namespace litecode::ui
