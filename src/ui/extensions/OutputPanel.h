#pragma once
#include <QString>
#include <QWidget>

class QPlainTextEdit;
class QToolButton;
namespace litecode::extensions {
class WorkbenchOutput;
}
namespace litecode::ui::components {
class ComboBox;
} // namespace litecode::ui::components
namespace litecode::ui {
// Workbench presentation only. The extension host owns output-channel lifetime
// and persistence; this view never loads plugins or writes their log files.
class OutputPanel final : public QWidget {
  public:
    OutputPanel(extensions::WorkbenchOutput& output, QWidget* parent = nullptr);
    void selectChannel(const QString& id);
    QString selectedChannel() const;
    void focusOutput();
    void setTextFont(const QFont& font);
    QWidget* controlsWidget() const { return toolbar_; }

  private:
    void refreshChannels();
    void refreshSelection();
    extensions::WorkbenchOutput& output_;
    components::ComboBox* channels_{};
    QWidget* toolbar_{};
    QToolButton* clear_{};
    QPlainTextEdit* text_{};
};
} // namespace litecode::ui
