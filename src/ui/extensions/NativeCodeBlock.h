#pragma once
#include <QFrame>
class QPlainTextEdit;
namespace litecode::ui {
class NativeCodeBlock final : public QFrame {
  public:
    NativeCodeBlock(const QString& code, const QString& language, QWidget* parent);
    QString code() const;
    void setCodeFont(const QFont& font);
    static int heightForCode(const QString& code, const QFont& font);
    QSize sizeHint() const override;

  private:
    QPlainTextEdit* editor_{};
};
} // namespace litecode::ui
