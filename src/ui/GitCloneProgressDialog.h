#pragma once
#include <QDialog>
namespace litecode::workspace {
class GitService;
}
namespace litecode::ui {
class GitCloneProgressDialog final : public QDialog {
  public:
    explicit GitCloneProgressDialog(workspace::GitService* git, QWidget* parent = nullptr);
    void reject() override;

  private:
    workspace::GitService* git_{};
};
} // namespace litecode::ui
