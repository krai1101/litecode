#pragma once
#include <QString>
#include <functional>
class QWidget;
namespace litecode::ui {
void requestGitRepository(QWidget* owner, const QString& workspace, const QString& current,
                          const std::function<void(const QString&)>& selected);
}
