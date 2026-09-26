#pragma once

#include <QFontDialog>

class QFrame;
class QResizeEvent;

namespace litecode::ui {

class DialogHeader;

class EditorFontDialog final : public QFontDialog {
    Q_OBJECT

  public:
    EditorFontDialog(const QFont& currentFont, const QFont& defaultFont, QWidget* parent = nullptr,
                     const QString& title = {});

    [[nodiscard]] bool restoreDefaultsSelected() const;

  protected:
    void resizeEvent(QResizeEvent* event) override;

  private:
    DialogHeader* header_{};
    QFrame* headerSeparator_{};
    bool restoringDefaults_{false};
    bool restoreDefaultsSelected_{false};
};

} // namespace litecode::ui
