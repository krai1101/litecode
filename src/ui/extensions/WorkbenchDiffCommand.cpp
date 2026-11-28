#include "ui/extensions/WorkbenchDiffCommand.h"
#include "ui/EditorArea.h"
#include "ui/extensions/WorkbenchDocumentOpenService.h"
#include <QJsonObject>
#include <stdexcept>
namespace litecode::ui {
WorkbenchDiffCommand::WorkbenchDiffCommand(EditorArea* editors,
                                           WorkbenchDocumentOpenService* documents, QObject* parent)
    : QObject(parent), editors_(editors), documents_(documents) {}
QFuture<QJsonValue> WorkbenchDiffCommand::execute(const QString& owner, const QJsonArray& args) {
    if (args.size() < 2 || args.size() > 4 || !args[0].isString() || !args[1].isString() ||
        (args.size() > 2 && !args[2].isNull() && !args[2].isString()) ||
        (args.size() > 3 && !args[3].isNull() && !args[3].isObject()))
        throw std::invalid_argument(
            "vscode.diff expects left URI, right URI, optional title and options.");
    const QUrl left(args[0].toString(), QUrl::StrictMode),
        right(args[1].toString(), QUrl::StrictMode);
    if (!left.isValid() || left.scheme().isEmpty() || !right.isValid() || right.scheme().isEmpty())
        throw std::invalid_argument("vscode.diff requires absolute resource URIs.");
    const auto options = args.size() > 3 ? args[3].toObject() : QJsonObject{};
    for (const auto& key : {QStringLiteral("preserveFocus"), QStringLiteral("preview")})
        if (options.contains(key) && !options[key].isBool())
            throw std::invalid_argument("Invalid diff editor options.");
    if (options.contains("viewColumn") && options["viewColumn"].toInt() != 1 &&
        options["viewColumn"].toInt() != -1)
        throw std::invalid_argument(
            "This diff command currently opens in the active editor group.");
    if (options.contains("selection"))
        throw std::invalid_argument("Diff selection options are not supported yet.");
    if (!documents_ || !editors_)
        throw std::runtime_error("Diff editor service stopped.");
    const auto title = args.size() > 2 ? args[2].toString() : QString{};
    const bool preserveFocus = options["preserveFocus"].toBool();
    return documents_->open(left, {}, owner)
        .then(this,
              [this, owner, right, title, preserveFocus](extensions::TextDocumentSnapshot before) {
                  if (!documents_)
                      throw std::runtime_error("Document service stopped.");
                  return documents_->open(right, {}, owner)
                      .then(this,
                            [this, before, title,
                             preserveFocus](extensions::TextDocumentSnapshot after) -> QJsonValue {
                                if (!editors_)
                                    throw std::runtime_error("Editor service stopped.");
                                editors_->openResourceComparison(before.uri, after.uri, before.text,
                                                                 after.text, title, preserveFocus);
                                return QJsonValue(QJsonValue::Undefined);
                            });
              })
        .unwrap();
}
} // namespace litecode::ui
