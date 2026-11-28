#include "core/SettingsService.h"
#include "extensions/UriWire.h"
#include "ui/GitCredentialBroker.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/extensions/NativeUriBroker.h"
#include "ui/extensions/NativeUriDispatcher.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"
#ifdef Q_OS_WIN
#include "WindowsJumpList.h"
#endif

#include <QApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLoggingCategory>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char* argv[]) {
    QElapsedTimer startupTimer;
    startupTimer.start();

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LiteCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LiteCode"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LITECODE_VERSION));
#ifdef Q_OS_LINUX
    QApplication::setDesktopFileName(QStringLiteral("litecode-url-handler"));
#endif
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/litecode.svg")));
    application.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    // A file-backed settings profile keeps benchmarks and automated runs isolated from the
    // user's restored workspace. Normal launches continue to use the native platform store.
    const QString settingsFile = qEnvironmentVariable("LITECODE_SETTINGS_FILE");
    const QStringList arguments = application.arguments();
    QList<QUrl> incomingUris;
    for (int index = 1; index < arguments.size(); ++index) {
        const bool explicitUri = arguments.at(index) == "--open-url";
        if (explicitUri && ++index >= arguments.size())
            break;
        const QUrl uri(arguments.at(index), QUrl::StrictMode);
        if (uri.isValid() && uri.scheme().compare(litecode::extensions::wire::AppUriScheme,
                                                  Qt::CaseInsensitive) == 0)
            incomingUris.append(uri);
    }
    // Profile identity is independent of workspace: OAuth returns to the last
    // focused workbench, not a new window restoring an unrelated workspace.
    const QString uriProfile = settingsFile.isEmpty() ? QStringLiteral("default")
                                                      : QFileInfo(settingsFile).absoluteFilePath();
    if (!arguments.contains(QStringLiteral("--new-window")) && !incomingUris.isEmpty()) {
        const auto accepted = litecode::ui::NativeUriBroker::forward(uriProfile, incomingUris);
        incomingUris = incomingUris.mid(accepted);
        if (incomingUris.isEmpty())
            return 0;
    }
    auto& uriDispatcher = litecode::ui::NativeUriDispatcher::forApplication();
    QObject::connect(&uriDispatcher, &litecode::ui::NativeUriDispatcher::routingError, &application,
                     [](const QString& message) { qWarning().noquote() << message; });
    litecode::ui::NativeUriBroker uriBroker(uriProfile, &application);
    if (!uriBroker.listen(
            [&uriDispatcher](const QUrl& uri) { return uriDispatcher.dispatchUri(uri); }))
        qWarning("Could not start desktop URI routing.");
    QObject::connect(&uriDispatcher, &litecode::ui::NativeUriDispatcher::windowActivated,
                     &uriBroker, &litecode::ui::NativeUriBroker::activated);
    for (const auto& uri : incomingUris)
        uriDispatcher.dispatchUri(uri);
    litecode::core::SettingsService settings(settingsFile);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const bool darkTheme = settings.theme() != QStringLiteral("light");
    litecode::ui::Theme::apply(application, darkTheme);
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch workspaceSearch;
    litecode::workspace::WorkspaceSearch quickOpenSearch;
    litecode::workspace::WorkspaceReplace workspaceReplace;
    litecode::ui::WorkbenchServices services{workspaceSearch, quickOpenSearch, workspaceReplace};
    const bool restorePreviousSession = !arguments.contains(QStringLiteral("--new-window"));
    litecode::ui::MainWindow window(settings, workspace, services, restorePreviousSession);
    const int folderOption = arguments.indexOf(QStringLiteral("--folder"));
    if (!restorePreviousSession && folderOption >= 0 && folderOption + 1 < arguments.size()) {
        const QFileInfo folder(arguments.at(folderOption + 1));
        if (folder.isAbsolute() && folder.isDir()) {
            QString error;
            if (!window.openWorkspaceFolder(folder.absoluteFilePath(), &error))
                qWarning().noquote() << "Could not open requested folder:" << error;
        }
    }
    const int fileOption = arguments.indexOf(QStringLiteral("--file"));
    if (fileOption >= 0 && fileOption + 1 < arguments.size()) {
        const QFileInfo file(arguments.at(fileOption + 1));
        if (file.isAbsolute() && file.isFile()) {
            QString error;
            if (!window.openFile(file.absoluteFilePath(), &error))
                qWarning().noquote() << "Could not open requested file:" << error;
        }
    }
#ifdef Q_OS_WIN
    if (settingsFile.isEmpty()) {
        auto* jumpListRefresh = new QTimer(&window);
        jumpListRefresh->setSingleShot(true);
        jumpListRefresh->setInterval(350);
        QObject::connect(&window, &litecode::ui::MainWindow::recentPathsChanged, jumpListRefresh,
                         qOverload<>(&QTimer::start));
        QObject::connect(jumpListRefresh, &QTimer::timeout, &window,
                         [&settings] { litecode::app::publishWindowsJumpList(settings); });
        jumpListRefresh->start();
    }
#endif
    litecode::ui::GitCredentialBroker credentials(&window, &application);
    QObject::connect(
        &window, &litecode::ui::MainWindow::firstFramePresented, &application, [&startupTimer] {
            qInfo().noquote() << QStringLiteral("startup.first_frame_ms=%1")
                                     .arg(startupTimer.nsecsElapsed() / 1'000'000.0, 0, 'f', 2);
        });
    window.show();

    return application.exec();
}
