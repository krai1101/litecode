#include "extensions/ChatApi.h"
#include "extensions/HostChatParticipant.h"
#include <QCoreApplication>
#include <QPromise>
#include <QtTest>
using namespace litecode::extensions;
namespace {
class Followups final : public ChatFollowupProvider {
  public:
    ProviderResult<QVector<ChatFollowup>> provideFollowups(ChatResult, ChatContext, CancellationToken) override {
        return QVector<ChatFollowup>{{"Question", {}, {}, {}}};
    }
};
ChatRequestHandler handler() {
    return [](ChatRequest, ChatContext, std::shared_ptr<ChatResponseStream>, CancellationToken) -> ChatHandlerResult { return std::monostate{}; };
}
}
class ChatParticipantTests final : public QObject {
    Q_OBJECT
  private slots:
    void propertiesUseMatchingNamesAndCoalesceMetadata() {
        QObject dispatcher; int updates = 0; bool hasFollowups = false; QJsonValue icon;
        HostChatParticipant participant(dispatcher, "fixture.chat", handler(), [] {},
            [&](const QJsonValue& value, bool followups) { ++updates; icon = value; hasFollowups = followups; });
        QVERIFY(participant.requestHandler()); QVERIFY(!participant.followupProvider()); QVERIFY(!participant.iconPath());
        participant.requestHandler(handler());
        participant.iconPath(IconPath{QUrl("file:///first.svg")});
        participant.iconPath(IconPath{QUrl("file:///last.svg")});
        auto provider = std::make_shared<Followups>(); participant.followupProvider(provider);
        QCOMPARE(participant.followupProvider(), std::shared_ptr<ChatFollowupProvider>(provider));
        QCOMPARE(updates, 0); QTRY_COMPARE(updates, 1);
        QCOMPARE(icon.toString(), QString("file:///last.svg")); QVERIFY(hasFollowups);
        participant.iconPath({}); participant.followupProvider({});
        QTRY_COMPARE(updates, 2); QVERIFY(icon.isUndefined()); QVERIFY(!hasFollowups);
        QVERIFY_EXCEPTION_THROWN(participant.requestHandler({}), std::invalid_argument);
    }
    void disposalCancelsQueuedUpdatesAndIsIdempotent() {
        QObject dispatcher; int removed = 0, updates = 0, feedback = 0;
        HostChatParticipant participant(dispatcher, "fixture.chat", handler(), [&] { ++removed; },
            [&](const QJsonValue&, bool) { ++updates; });
        auto subscription = participant.onDidReceiveFeedback()([&](const ChatResultFeedback&) { ++feedback; });
        participant.followupProvider(std::make_shared<Followups>());
        participant.iconPath(IconPath{QUrl("file:///icon.svg")});
        participant.fireFeedback({}); QCOMPARE(feedback, 1);
        participant.dispose(); participant.dispose(); QCOMPARE(removed, 1);
        QCoreApplication::processEvents(); QCOMPARE(updates, 0);
        participant.fireFeedback({}); QCOMPARE(feedback, 1);
        QVERIFY(!participant.followupProvider()); QVERIFY(participant.requestHandler()); QVERIFY(participant.iconPath());
        participant.iconPath({}); participant.requestHandler(handler()); participant.followupProvider(std::make_shared<Followups>());
        QCoreApplication::processEvents(); QCOMPARE(updates, 0); subscription.dispose();
    }
    void feedbackListenerCanDestroyParticipantSafely() {
        QObject dispatcher; int removed = 0;
        auto participant = std::make_unique<HostChatParticipant>(dispatcher, "fixture.chat", handler(), [&] { ++removed; },
            [](const QJsonValue&, bool) {});
        auto subscription = participant->onDidReceiveFeedback()([&](const ChatResultFeedback&) { participant.reset(); });
        participant->fireFeedback({}); QVERIFY(!participant); QCOMPARE(removed, 1); subscription.dispose();
    }
};
QTEST_GUILESS_MAIN(ChatParticipantTests)
#include "tst_extension_chat_participant.moc"
