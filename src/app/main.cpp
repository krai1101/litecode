#include "core/SettingsService.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QIcon>
#include <QLoggingCategory>
#include <QStyleFactory>

int main(int argc, char* argv[]) {
    QElapsedTimer startupTimer;
    startupTimer.start();

    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LiteCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LiteCode"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/litecode.svg")));
    application.setFont(QFont(QStringLiteral("Segoe UI"), 9));
    // A file-backed settings profile keeps benchmarks and automated runs isolated from the
    // user's restored workspace. Normal launches continue to use the native platform store.
    const QString settingsFile = qEnvironmentVariable("LITECODE_SETTINGS_FILE");
    litecode::core::SettingsService settings(settingsFile);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    const bool darkTheme = settings.theme() != QStringLiteral("light");
    litecode::ui::Theme::apply(application, darkTheme);
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch workspaceSearch;
    litecode::workspace::WorkspaceSearch quickOpenSearch;
    litecode::workspace::WorkspaceReplace workspaceReplace;
    litecode::ui::WorkbenchServices services{workspaceSearch, quickOpenSearch, workspaceReplace};
    const bool restorePreviousSession =
        !application.arguments().contains(QStringLiteral("--new-window"));
    litecode::ui::MainWindow window(settings, workspace, services, restorePreviousSession);
    QObject::connect(
        &window, &litecode::ui::MainWindow::firstFramePresented, &application, [&startupTimer] {
            qInfo().noquote() << QStringLiteral("startup.first_frame_ms=%1")
                                     .arg(startupTimer.nsecsElapsed() / 1'000'000.0, 0, 'f', 2);
        });
    window.show();

    return application.exec();
}
