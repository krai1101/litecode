#include "extensions/ExtensionManager.h"
#include "ui/extensions/NativeViewService.h"
#include "ui/Theme.h"
#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QToolBar>
#include <QtTest>

using namespace litecode;
class CodexAuthUiTests final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        ui::Theme::apply(*qApp, qEnvironmentVariableIsSet("LITECODE_NATIVE_DARK"));
    }
    void authenticationThroughSeparateHost() {
        QTemporaryDir extensions, workspace;
        const QFileInfo plugin(LITECODE_CODEX_PLUGIN);
        const QDir package(extensions.filePath("litecode.codex-1.0.0"));
        QVERIFY(QDir().mkpath(package.filePath("runtime/bin")));
        QVERIFY(QFile::copy(plugin.filePath(), package.filePath(plugin.fileName())));
        QVERIFY(QFile::copy(plugin.dir().filePath("codex.svg"), package.filePath("codex.svg")));
        const QDir icons(plugin.dir().filePath("icons"));
        QVERIFY(QDir().mkpath(package.filePath("icons")));
        for (const auto& name : icons.entryList(QDir::Files))
            QVERIFY(QFile::copy(icons.filePath(name), package.filePath("icons/" + name)));
#ifdef Q_OS_WIN
        const auto runtime = package.filePath("runtime/bin/codex.exe");
#else
        const auto runtime = package.filePath("runtime/bin/codex");
#endif
        QVERIFY(QFile::copy(LITECODE_CODEX_FIXTURE, runtime));
        extensions::ExtensionManager manager(extensions.path());
        manager.setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager.setWorkspace(workspace.path());
        QMainWindow window;
        QToolBar activity(&window);
        window.addToolBar(&activity);
        ui::NativeViewService service(manager.nativeViews(), window, &activity);
        manager.discover();
        QVERIFY(manager.setEnabled("litecode.codex", true));
        window.resize(540, 860);
        window.show();
        service.openView("litecode.codex.view");
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QAbstractButton*>("codex.settings") &&
            window.findChild<QAbstractButton*>("codex.settings")->isEnabled(), 15000);
        const auto openLogout = [&] {
            auto* settings = window.findChild<QAbstractButton*>("codex.settings");
            settings->click();
            auto* menu = settings->findChild<QMenu*>();
            if (!menu) return false;
            auto* logout = menu->findChild<QAction*>("codex.menu.signOut");
            if (!logout || !logout->isEnabled()) return false;
            logout->trigger();
            menu->close();
            return true;
        };
        const auto capture = [&](const QString& name) {
            const auto dir = qEnvironmentVariable("LITECODE_AUTH_CAPTURE_DIR");
            if (!dir.isEmpty()) {
                QDir().mkpath(dir);
                auto* dialog = window.findChild<QDialog*>("codex.auth.logoutDialog");
                if (name == "confirm" && dialog) {
                    QTest::qWait(60);
                    dialog->grab().save(QDir(dir).filePath(name + ".png"));
                } else
                    window.grab().save(QDir(dir).filePath(name + ".png"));
            }
        };
        QVERIFY(openLogout());
        QTRY_VERIFY(window.findChild<QDialog*>("codex.auth.logoutDialog"));
        QVERIFY(!window.findChild<QWidget*>("codex.auth.root"));
        capture("confirm");
        window.findChild<QAbstractButton*>("codex.auth.logoutCancel")->click();
        QTRY_VERIFY(!window.findChild<QDialog*>("codex.auth.logoutDialog"));
        QVERIFY(window.findChild<QAbstractButton*>("codex.settings"));
        QVERIFY(openLogout());
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.auth.logoutConfirm"));
        window.findChild<QAbstractButton*>("codex.auth.logoutConfirm")->click();
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.auth.chatgpt"));
        QTRY_VERIFY(!window.findChild<QWidget*>("codex.prompt"));
        QTest::qWait(100);
        capture("signed-out");
        window.findChild<QAbstractButton*>("codex.auth.apiKey")->click();
        QTRY_VERIFY(window.findChild<QLineEdit*>("codex.auth.key"));
        auto* key = window.findChild<QLineEdit*>("codex.auth.key");
        QCOMPARE(key->echoMode(), QLineEdit::Password);
        QVERIFY(!window.findChild<QAbstractButton*>("codex.auth.keySubmit")->isEnabled());
        QTest::keyClicks(key, "fixture-api-key");
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.auth.keySubmit")->isEnabled());
        capture("api-key");
        window.resize(340, 620);
        QTest::qWait(60);
        capture("api-key-narrow");
        QCOMPARE(window.findChild<QLineEdit*>("codex.auth.key"), key);
        QCOMPARE(key->text(), QString("fixture-api-key"));
        window.findChild<QAbstractButton*>("codex.auth.keySubmit")->click();
        QTRY_VERIFY(window.findChild<QAbstractButton*>("codex.settings"));
        QTRY_VERIFY(!window.findChild<QWidget*>("codex.auth.root"));
        QVERIFY(manager.extensions().first().error.isEmpty());
        QVERIFY(manager.setEnabled("litecode.codex", false));
    }
};
QTEST_MAIN(CodexAuthUiTests)
#include "tst_codex_auth_ui.moc"
