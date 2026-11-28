#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QEvent;
class QLabel;
class QLineEdit;
class QSplitter;
class QToolButton;

namespace litecode::debug {
class DebugSession;
}

namespace litecode::ui {

// The four resizable Run and Debug panes. Inspection requests stay here so the
// launch/configuration view does not own variable-tree or watch-expression UI.
class DebugInspectorWidget final : public QWidget {
    Q_OBJECT

  public:
    explicit DebugInspectorWidget(debug::DebugSession& session, QWidget* parent = nullptr);
    void setBreakpoints(const QMap<QString, QSet<int>>& breakpoints);
    void setInspectionVisible(bool visible);
    void clearWorkspace();
    void setTargetName(const QString& name) { targetName_ = name; }
    void setWorkspace(const QString& path) { workspace_ = path; }
    int selectedFrame() const { return selectedFrame_; }

  signals:
    void openLocationRequested(const QString& path, int line);
    void removeBreakpointRequested(const QString& path, int line);
    void activeFrameChanged(const QString& path, int line);

  protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void selectFrame(QTreeWidgetItem* frame);
    void refreshWatch();
    void addWatchExpression();
    void finishWatchExpression(bool accept);
    void requestVariables(QTreeWidgetItem* item);
    void handleInspection(int requestId, const QString& command, const QJsonObject& body);
    void clearInspection();
    void updateTreePalettes();
    void refreshBreakpointRows();
    void updateBreakpointHeight();

    debug::DebugSession& session_;
    QSplitter* sections_{};
    QList<QWidget*> inspectionSections_;
    QList<int> inspectionSizes_;
    QWidget* breakpointHeader_{};
    QWidget* breakpointBody_{};
    bool inspectionVisible_{true};
    QTreeWidget* variables_{};
    QTreeWidget* watch_{};
    QTreeWidget* callStack_{};
    QTreeWidget* breakpoints_{};
    QToolButton* watchHeader_{};
    QTreeWidgetItem* watchDraft_{};
    QLineEdit* watchDraftInput_{};
    QString targetName_;
    QString pauseDescription_;
    QStringList watchExpressions_;
    QString workspace_;
    QJsonArray exceptionFilterDefinitions_;
    QSet<QString> enabledExceptionFilters_;
    QMap<QString, QSet<int>> sourceBreakpoints_;
    QHash<int, QTreeWidgetItem*> variableRequests_;
    QHash<int, QTreeWidgetItem*> watchRequests_;
    int scopeRequest_{};
    int selectedFrame_{-1};
};

} // namespace litecode::ui
