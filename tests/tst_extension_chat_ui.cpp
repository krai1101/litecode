#include "extensions/ExtensionManager.h"
#include "ui/chat/ParticipantChatPanel.h"
#include "ui/chat/ParticipantResponseView.h"
#include "ui/EditorArea.h"
#include "ui/controllers/EditorSessionController.h"
#include "editor/ScintillaEditor.h"
#include <QComboBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QtTest>
#include <algorithm>
#include <memory>

using namespace litecode;
class ExtensionChatUiTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory_;
    std::unique_ptr<extensions::ExtensionManager> manager_;
    std::unique_ptr<ui::chat::ParticipantChatPanel> panel_;
    QLineEdit* input_{};
    QPushButton *send_{}, *stop_{};
    void send(const QString& prompt) {
        input_->setText(prompt);
        QTest::mouseClick(send_, Qt::LeftButton);
    }
    static bool messageContains(const QSignalSpy& messages, const QString& text) {
        return std::any_of(messages.begin(), messages.end(), [&](const auto& item) {
            return item.first().toString().contains(text);
        });
    }
  private slots:
    void init() {
        directory_ = std::make_unique<QTemporaryDir>(); QVERIFY(directory_->isValid());
        QVERIFY(QFile::copy(LITECODE_TEST_PLUGIN, directory_->filePath(QFileInfo(LITECODE_TEST_PLUGIN).fileName())));
        manager_ = std::make_unique<extensions::ExtensionManager>(directory_->path());
        manager_->setHostExecutablePath(LITECODE_EXTENSION_HOST);
        manager_->setWorkspace(directory_->path()); manager_->discover();
        QVERIFY(manager_->setEnabled("tests.native", true));
        panel_ = std::make_unique<ui::chat::ParticipantChatPanel>(manager_.get());
        panel_->resize(900, 800); panel_->show();
        input_ = panel_->findChild<QLineEdit*>("chatPromptInput");
        send_ = panel_->findChild<QPushButton*>("chatSendButton");
        stop_ = panel_->findChild<QPushButton*>("chatStopButton");
        QVERIFY(input_); QVERIFY(send_); QVERIFY(stop_);
    }
    void cleanup() { panel_.reset(); manager_.reset(); directory_.reset(); }
    void streamsHistoryFollowupsAndFeedbackThroughVisiblePanel() {
        auto* commands = panel_->findChild<QComboBox*>("chatCommandSelector"); QVERIFY(commands);
        commands->setCurrentIndex(commands->findData("explain"));
        QSignalSpy finished(manager_.get(), &extensions::ExtensionManager::chatRequestFinished);
        QSignalSpy messages(manager_.get(), &extensions::ExtensionManager::informationMessage);
        send("ui-stream");
        QTRY_VERIFY(panel_->findChild<QTextBrowser*>("chatResponseMarkdown"));
        QVERIFY(stop_->isEnabled());
        auto* first = panel_->findChild<ui::chat::ParticipantResponseView*>(); QVERIFY(first);
        QTRY_VERIFY(first->findChild<QTextBrowser*>("chatResponseMarkdown")->toPlainText().contains("streamed response."));
        QTRY_COMPARE(finished.size(), 1);
        QTRY_VERIFY(first->findChild<QPushButton*>("chatFollowupButton"));
        QTest::mouseClick(first->findChild<QPushButton*>("chatFollowupButton"), Qt::LeftButton);
        QTRY_COMPARE(finished.size(), 2);
        QTRY_COMPARE(panel_->findChildren<ui::chat::ParticipantResponseView*>().size(), 2);
        auto* second = panel_->findChildren<ui::chat::ParticipantResponseView*>().last();
        QTRY_VERIFY(second->findChild<QTextBrowser*>("chatResponseMarkdown"));
        QVERIFY(second->findChild<QTextBrowser*>("chatResponseMarkdown")->toPlainText().contains("history=2 prompt=Ask another question"));
        QCOMPARE(finished.last()[1].toJsonObject().value("metadata").toObject().value("command").toString(), QString("explain"));
        QTRY_VERIFY(second->findChild<QPushButton*>("chatFeedbackHelpful"));
        QTest::mouseClick(second->findChild<QPushButton*>("chatFeedbackHelpful"), Qt::LeftButton);
        QTRY_VERIFY(messageContains(messages, "Chat feedback 1 command=explain"));
        QVERIFY(!stop_->isEnabled());
    }
    void resourceLocationsAndCommandButtonsReachActualWorkbench() {
        const auto path = directory_->filePath("fixture.txt");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("zero\none\n0123456789\nend"); }
        ui::EditorArea area;
        ui::EditorSessionController controller(area);
        QString openError;
        connect(panel_.get(), &ui::chat::ParticipantChatPanel::resourceActivated, &controller,
            [&](const QUrl& uri, int line, int column) { controller.openMatch(uri.toLocalFile(), line, column, 0, &openError); });
        QSignalSpy locations(panel_.get(), &ui::chat::ParticipantChatPanel::resourceActivated);
        QSignalSpy finished(manager_.get(), &extensions::ExtensionManager::chatRequestFinished);
        QSignalSpy messages(manager_.get(), &extensions::ExtensionManager::informationMessage);
        send("ui-resources"); QTRY_COMPARE(finished.size(), 1);
        QTRY_VERIFY(panel_->findChild<QPushButton*>("chatResponseAnchorButton"));
        QTest::mouseClick(panel_->findChild<QPushButton*>("chatResponseAnchorButton"), Qt::LeftButton);
        QTRY_COMPARE(locations.size(), 1); QVERIFY2(openError.isEmpty(), qPrintable(openError));
        QCOMPARE(locations.first()[1].toInt(), 3); QCOMPARE(locations.first()[2].toInt(), 4);
        QTRY_VERIFY(area.findChild<editor::ScintillaEditor*>());
        auto* editorWidget = area.findChild<editor::ScintillaEditor*>();
        QTRY_COMPARE(editorWidget->selectionByteRange(), (QPair<qint64, qint64>{12, 12}));
        QTest::mouseClick(panel_->findChild<QPushButton*>("chatResponseReferenceButton"), Qt::LeftButton);
        QTRY_COMPARE(locations.size(), 2);
        QTest::mouseClick(panel_->findChild<QPushButton*>("chatResponseCommandButton"), Qt::LeftButton);
        QTRY_VERIFY(messageContains(messages, "Hello from native C++"));
        auto* tree = panel_->findChild<QTreeWidget*>("chatResponseFileTree"); QVERIFY(tree);
        tree->setCurrentItem(tree->topLevelItem(0));
        QTest::keyClick(tree, Qt::Key_Return);
        QTRY_COMPARE(locations.size(), 3);
        QCOMPARE(locations.last()[0].toUrl(), QUrl::fromLocalFile(path));
        QTRY_COMPARE(editorWidget->selectionByteRange(), (QPair<qint64, qint64>{0, 0}));
    }
    void filteredResponseRemovesVisibleAndActionableContent() {
        QSignalSpy finished(manager_.get(), &extensions::ExtensionManager::chatRequestFinished);
        send("ui-filtered"); QTRY_COMPARE(finished.size(), 1);
        auto* response = panel_->findChild<ui::chat::ParticipantResponseView*>(); QVERIFY(response);
        QTRY_COMPARE(response->findChild<QLabel*>("chatResponseError")->text(), QString("Filtered fixture response"));
        QTRY_COMPARE(response->findChildren<QTextBrowser*>().size(), 0);
        QVERIFY(response->findChildren<QPushButton*>("chatResponseCommandButton").isEmpty());
        QVERIFY(response->findChildren<QPushButton*>("chatResponseAnchorButton").isEmpty());
        QCOMPARE(response->findChild<QLabel*>("chatResponseError")->text(), QString("Filtered fixture response"));
    }
    void cancellationFailureAndOwnerLoss_data() {
        QTest::addColumn<QString>("mode");
        for (const auto& mode : {"cancel", "throw", "disable", "crash", "new-chat"}) QTest::newRow(mode) << QString(mode);
    }
    void cancellationFailureAndOwnerLoss() {
        QFETCH(QString, mode);
        QSignalSpy finished(manager_.get(), &extensions::ExtensionManager::chatRequestFinished);
        send(mode == "throw" ? "throw" : "cancel");
        if (mode != "throw") {
            QTRY_VERIFY(panel_->findChild<QTextBrowser*>("chatResponseMarkdown"));
            if (mode == "cancel") QTest::mouseClick(stop_, Qt::LeftButton);
            else if (mode == "disable") QVERIFY(manager_->setEnabled("tests.native", false));
            else if (mode == "crash") QVERIFY(manager_->requestCommand("tests.native.exitHost"));
            else QTest::mouseClick(panel_->findChild<QPushButton*>("chatNewSessionButton"), Qt::LeftButton);
        }
        QTRY_COMPARE(finished.size(), 1);
        if (mode == "new-chat") {
            QVERIFY(panel_->findChildren<ui::chat::ParticipantResponseView*>().isEmpty());
            QTest::qWait(300);
            QVERIFY(panel_->findChildren<ui::chat::ParticipantResponseView*>().isEmpty());
        } else {
            auto* response = panel_->findChild<ui::chat::ParticipantResponseView*>(); QVERIFY(response);
            QTRY_VERIFY(!response->findChild<QLabel*>("chatResponseError")->text().isEmpty());
        }
        QTRY_VERIFY(!stop_->isEnabled());
        if (mode == "disable" || mode == "crash") {
            if (mode == "crash") QVERIFY(manager_->setEnabled("tests.native", false));
            QTRY_VERIFY(manager_->setEnabled("tests.native", true));
        }
        input_->setText("after"); QTRY_VERIFY(send_->isEnabled());
        QTest::mouseClick(send_, Qt::LeftButton); QTRY_COMPARE(finished.size(), 2);
        QVERIFY(finished.last()[2].toString().isEmpty());
        if (mode == "new-chat") {
            auto* markdown = panel_->findChild<QTextBrowser*>("chatResponseMarkdown"); QVERIFY(markdown);
            QVERIFY(markdown->toPlainText().contains("history=0"));
        }
    }
};
QTEST_MAIN(ExtensionChatUiTests)
#include "tst_extension_chat_ui.moc"
