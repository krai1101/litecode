#pragma once
#include "core/GitPreferences.h"
#include <QString>
#include <optional>
class QWidget;
namespace litecode::ui {
std::optional<QString> requestGitBranchName(QWidget* owner, const QString& repository,
                                            const core::GitPreferences& preferences = {});
} // namespace litecode::ui
