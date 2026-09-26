#include "core/SettingsService.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

namespace litecode::tests {

class SettingsSessionTest final : public QObject {
    Q_OBJECT

  private slots:
    void emptyWindowDoesNotOverwriteSavedSession();
    void workspaceSessionCanClearOpenFiles();
    void searchHistoryIsScopedAndPersists();
    void fileEncodingDefaultsAndPersists();
    void autoGuessEncodingDefaultsAndPersists();
    void editorFontWeightDefaultsAndPersists();
    void editorFontSizeMigratesPointsToPixels();
    void emptyShortcutOverridePersistsAndCanBeReset();
    void individualSettingsResetToDefaults();
    void reportsUnwritableSettings();
    void unsupportedEncodingIsReportedAndFallsBack();
};

void SettingsSessionTest::emptyWindowDoesNotOverwriteSavedSession() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    core::SettingsService settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")));
    const QString workspace = QStringLiteral("C:/workspace");
    const QString file = QStringLiteral("C:/workspace/main.cpp");
    settings.saveSession(workspace, {file}, file);

    settings.saveSession({}, {}, {});

    QCOMPARE(settings.workspaceFolder(), workspace);
    QCOMPARE(settings.openFiles(), QStringList{file});
    QCOMPARE(settings.activeFile(), file);
}

void SettingsSessionTest::searchHistoryIsScopedAndPersists() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString file = directory.filePath(QStringLiteral("settings.ini"));
    {
        core::SettingsService settings(file);
        settings.saveSearchHistory(QStringLiteral("workspace-a"), QStringLiteral("editorFind"),
                                   {QStringLiteral("first"), QStringLiteral("second")});
        QVERIFY(settings.sync());
    }
    core::SettingsService reopened(file);
    QCOMPARE(reopened.searchHistory(QStringLiteral("workspace-a"), QStringLiteral("editorFind")),
             (QStringList{QStringLiteral("first"), QStringLiteral("second")}));
    QVERIFY(reopened.searchHistory(QStringLiteral("workspace-b"), QStringLiteral("editorFind"))
                .isEmpty());
    QVERIFY(reopened.searchHistory(QStringLiteral("workspace-a"), QStringLiteral("workspaceSearch"))
                .isEmpty());
}

void SettingsSessionTest::workspaceSessionCanClearOpenFiles() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    core::SettingsService settings(temporaryDirectory.filePath(QStringLiteral("settings.ini")));
    const QString workspace = QStringLiteral("C:/workspace");
    settings.saveSession(workspace, {QStringLiteral("C:/workspace/main.cpp")},
                         QStringLiteral("C:/workspace/main.cpp"));

    settings.saveSession(workspace, {}, {});

    QCOMPARE(settings.workspaceFolder(), workspace);
    QVERIFY(settings.openFiles().isEmpty());
    QVERIFY(settings.activeFile().isEmpty());
}

void SettingsSessionTest::fileEncodingDefaultsAndPersists() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("settings.ini"));

    {
        core::SettingsService settings(path);
        QCOMPARE(settings.fileEncoding(), core::TextEncoding::Utf8);
        settings.saveFileEncoding(core::TextEncoding::Windows1252);
        settings.sync();
    }
    core::SettingsService reloaded(path);
    QCOMPARE(reloaded.fileEncoding(), core::TextEncoding::Windows1252);

    QSettings stored(path, QSettings::IniFormat);
    QCOMPARE(stored.value(QStringLiteral("files/encoding")).toString(),
             QStringLiteral("windows1252"));

    const QString legacyPath = temporaryDirectory.filePath(QStringLiteral("legacy-settings.ini"));
    QSettings legacy(legacyPath, QSettings::IniFormat);
    legacy.setValue(QStringLiteral("files/encoding"),
                    static_cast<int>(core::TextEncoding::Windows1252));
    legacy.sync();
    core::SettingsService migrated(legacyPath);
    QCOMPARE(migrated.fileEncoding(), core::TextEncoding::Windows1252);
}

void SettingsSessionTest::autoGuessEncodingDefaultsAndPersists() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("settings.ini"));

    core::SettingsService settings(path);
    QVERIFY(!settings.fileAutoGuessEncoding());
    settings.saveFileAutoGuessEncoding(true);
    settings.sync();

    core::SettingsService reloaded(path);
    QVERIFY(reloaded.fileAutoGuessEncoding());
}

void SettingsSessionTest::editorFontWeightDefaultsAndPersists() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("settings.ini"));

    core::SettingsService settings(path);
    QCOMPARE(settings.editorFontWeight(), 400);
    settings.saveEditorFont(QStringLiteral("Consolas"), 13, 600);
    settings.sync();

    core::SettingsService reloaded(path);
    QCOMPARE(reloaded.editorFontWeight(), 600);
}

void SettingsSessionTest::editorFontSizeMigratesPointsToPixels() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("settings.ini"));

    QSettings legacy(path, QSettings::IniFormat);
    legacy.setValue(QStringLiteral("editor/fontSize"), 12);
    legacy.sync();
    QCOMPARE(core::SettingsService(path).editorFontSize(), 16);

    core::SettingsService settings(path);
    settings.saveEditorFont(QStringLiteral("Consolas"), 14);
    settings.sync();
    QCOMPARE(core::SettingsService(path).editorFontSize(), 14);
}

void SettingsSessionTest::emptyShortcutOverridePersistsAndCanBeReset() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString path = temporaryDirectory.filePath(QStringLiteral("settings.ini"));
    const QString commandId = QStringLiteral("file.openFile");

    {
        core::SettingsService settings(path);
        QVERIFY(!settings.hasCommandShortcut(commandId));
        settings.saveCommandShortcut(commandId, {});
        settings.sync();
    }
    {
        core::SettingsService settings(path);
        QVERIFY(settings.hasCommandShortcut(commandId));
        QVERIFY(settings.commandShortcut(commandId, QStringLiteral("Ctrl+O")).isEmpty());
        settings.removeCommandShortcut(commandId);
        settings.sync();
    }
    core::SettingsService reset(path);
    QVERIFY(!reset.hasCommandShortcut(commandId));
    QCOMPARE(reset.commandShortcut(commandId, QStringLiteral("Ctrl+O")), QStringLiteral("Ctrl+O"));
}

void SettingsSessionTest::individualSettingsResetToDefaults() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("settings.ini"));
    core::SettingsService settings(path);
    settings.saveEditorFont(QStringLiteral("Courier New"), 20, 700);
    settings.saveFileAutoGuessEncoding(true);
    settings.saveFileEncoding(core::TextEncoding::Windows1252);
    QVERIFY(settings.sync());

    settings.resetSetting(QStringLiteral("editor.fontSize"));
#ifdef Q_OS_MACOS
    QCOMPARE(core::SettingsService::defaultEditorFontSize(), 12);
#else
    QCOMPARE(core::SettingsService::defaultEditorFontSize(), 14);
#endif
    QCOMPARE(settings.editorFontSize(), core::SettingsService::defaultEditorFontSize());
    QCOMPARE(settings.editorFontFamily(), QStringLiteral("Courier New"));
    QCOMPARE(settings.editorFontWeight(), 700);
    settings.saveEditorFontWeight(600);
    QCOMPARE(settings.editorFontSize(), core::SettingsService::defaultEditorFontSize());
    settings.resetSetting(QStringLiteral("editor.fontFamily"));
    settings.resetSetting(QStringLiteral("editor.fontWeight"));
    settings.resetSetting(QStringLiteral("files.autoGuessEncoding"));
    settings.resetSetting(QStringLiteral("files.encoding"));
    QVERIFY(settings.sync());

    core::SettingsService reloaded(path);
    QCOMPARE(reloaded.editorFontFamily(), core::SettingsService::defaultEditorFontFamily());
    QCOMPARE(reloaded.editorFontWeight(), 400);
    QVERIFY(!reloaded.fileAutoGuessEncoding());
    QCOMPARE(reloaded.fileEncoding(), core::TextEncoding::Utf8);
    QSettings stored(path, QSettings::IniFormat);
    QVERIFY(!stored.contains(QStringLiteral("editor/fontSize")));
    QVERIFY(!stored.contains(QStringLiteral("files/encoding")));
}

void SettingsSessionTest::reportsUnwritableSettings() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    core::SettingsService settings(directory.path());
    settings.saveFileAutoGuessEncoding(true);
    QVERIFY(!settings.sync());
}

void SettingsSessionTest::unsupportedEncodingIsReportedAndFallsBack() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("user.ini"));
    QSettings stored(path, QSettings::IniFormat);
    stored.setValue(QStringLiteral("files/encoding"), QStringLiteral("ut8"));
    stored.sync();

    core::SettingsService settings(path);
    QVERIFY(!settings.userEncodingValid());
    QCOMPARE(settings.fileEncoding(), core::TextEncoding::Utf8);
    settings.saveFileEncoding(core::TextEncoding::Windows1252);
    QVERIFY(settings.sync());
    QVERIFY(settings.userEncodingValid());
    QCOMPARE(settings.fileEncoding(), core::TextEncoding::Windows1252);
}

} // namespace litecode::tests

QTEST_GUILESS_MAIN(litecode::tests::SettingsSessionTest)

#include "tst_settings_session.moc"
