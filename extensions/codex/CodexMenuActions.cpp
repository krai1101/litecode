#include "CodexViewProvider.h"
#include "CodexThreadLink.h"
#include "ConversationMarkdown.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
namespace litecode::codex {
bool CodexViewProvider::handleMenuAction(const extensions::NativeViewEvent& event) {
    if ((!event.controlId.startsWith("codex.menu.") &&
         !event.controlId.startsWith("codex.localMenu.")) || event.type != "clicked")
        return false;
    const auto id = event.controlId.section('.', -1);
    if (id.startsWith("copy") && (session_.threadId().isEmpty() || session_.opening()))
        return true;
    if (id == "rename")
        options("Rename");
    else if (id == "archive")
        options("Archive");
    else if (id == "copyDeeplink" || id == "copyWorkingDirectory") {
        QString text;
        try {
            text = id == "copyDeeplink"
                ? codexThreadLink(context_.env().uriScheme(), context_.extensionId(), session_.threadId())
                      .toString(QUrl::FullyEncoded)
                : QDir::toNativeSeparators(session_.threadWorkspace());
        } catch (const std::exception& error) {
            notice(QString::fromUtf8(error.what()));
            return true;
        }
        context_.env().clipboard().writeText(text).onFailed(
            this, [this](const std::exception& error) { notice(QString::fromUtf8(error.what())); });
    } else if (id == "copyMarkdown") {
        exportCodexMarkdown(session_.server(), session_.threadId(), session_.title())
            .then(this, [this](const QString& markdown) {
                context_.env().clipboard().writeText(markdown).onFailed(
                    this, [this](const std::exception& error) { notice(QString::fromUtf8(error.what())); });
            })
            .onFailed(this, [this](const std::exception& error) { notice(QString::fromUtf8(error.what())); });
    } else if (id == "signOut") {
        state_.confirmingLogout = true;
        render();
    }
    else if (id == "signIn")
        session_.signIn();
    else if (id == "cancelSignIn")
        session_.cancelSignIn();
    else if (id == "shortcuts")
        context_.commands().executeCommand("workbench.action.openGlobalKeybindings")
            .onFailed(this, [this](const std::exception& error) {
                notice(QString::fromUtf8(error.what()));
                return QJsonValue{};
            });
    else if (id == "settings") {
        auto home = qEnvironmentVariable("CODEX_HOME");
        if (home.isEmpty())
            home = QDir::home().filePath(".codex");
        const auto path = QDir(home).filePath("config.toml");
        if (!QFile::exists(path)) {
            QSaveFile file(path);
            if (!QDir().mkpath(home) || !file.open(QIODevice::WriteOnly) ||
                file.write("# Codex configuration\n") < 0 || !file.commit()) {
                notice("Could not open the Codex configuration file.");
                return true;
            }
        }
        context_.openFile(path);
    }
    return true;
}
} // namespace litecode::codex
