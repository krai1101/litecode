#include "extensions/HostWindowState.h"
#include "extensions/WindowStateWire.h"
#include "extensions/ExtensionManager.h"
#include "ui/extensions/WindowStateService.h"
#include <QApplication>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>
using namespace litecode::extensions;
class WindowStateTests final : public QObject {
    Q_OBJECT
  private slots:
    void wireRejectsCoercionAndMissingProperties() {
        QVERIFY(!wire::decodeWindowState({{"focused", 1}, {"active", true}}));
        QVERIFY(!wire::decodeWindowState({{"focused", true}}));
        const auto state = wire::decodeWindowState(wire::encodeWindowState({false, true}));
        QVERIFY(state); QCOMPARE(*state, (WindowState{false, true}));
    }
    void propertyEventsAreDeduplicatedSnapshotsAndDisposedOnShutdown() {
        HostWindowState host; QVector<WindowState> events;
        auto subscription = host.onDidChangeWindowState()([&](WindowState state) {
            events.append(state); QCOMPARE(host.state(), state);
        });
        const auto initial = host.state();
        host.accept({false, false}); host.accept({false, false});
        QCOMPARE(initial, (WindowState{true, true}));
        QCOMPARE(events, (QVector<WindowState>{{false, true}, {false, false}}));
        subscription.dispose(); host.accept({true, false}); QCOMPARE(events.size(), 2);
        auto stopped = host.onDidChangeWindowState()([&](WindowState state) { events.append(state); });
        host.shutdown(); host.accept({true, true});
        QCOMPARE(events.size(), 2); QCOMPARE(host.state(), (WindowState{true, false}));
        stopped.dispose();
    }
    void listenerMayDestroyFacadeWithoutInvalidatingCurrentDelivery() {
        auto host = std::make_unique<HostWindowState>(); int calls = 0;
        auto subscription = host->onDidChangeWindowState()([&](WindowState) { ++calls; host.reset(); });
        host->accept({false, false});
        QCOMPARE(calls, 1); QVERIFY(!host); subscription.dispose();
    }
    void activityUsesTwoThirtySecondIntervalsAndOnlyRealInputResetsIt() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ExtensionManager manager(directory.path()); QWidget window, unrelated;
        litecode::ui::WindowStateService service(manager, window);
        const bool focused = manager.windowState().focused;
        auto* timer = service.findChild<QTimer*>("windowActivityIntervals"); QVERIFY(timer);
        auto* inactive = service.findChild<QTimer*>("windowInactiveDebounce"); QVERIFY(inactive);
        QCOMPARE(inactive->interval(), 10'000);
        QCOMPARE(timer->interval(), 30'000); QVERIFY(timer->isActive());
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QVERIFY(manager.windowState().active); QVERIFY(!inactive->isActive());
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QVERIFY(manager.windowState().active); QVERIFY(inactive->isActive());
        QVERIFY(QMetaObject::invokeMethod(inactive, "timeout", Qt::DirectConnection));
        QVERIFY(!manager.windowState().active); QVERIFY(!timer->isActive());
        QCOMPARE(manager.windowState().focused, focused);
        QKeyEvent outside(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
        QApplication::sendEvent(&unrelated, &outside); QVERIFY(!manager.windowState().active);
        QKeyEvent input(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
        QApplication::sendEvent(&window, &input); QVERIFY(manager.windowState().active); QVERIFY(timer->isActive());
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QApplication::sendEvent(&window, &input);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QVERIFY(manager.windowState().active);
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(inactive, "timeout", Qt::DirectConnection));
        QVERIFY(!manager.windowState().active);
    }
    void progressExtendsExistingActivityWithoutWakingAnInactiveUser() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ExtensionManager manager(directory.path()); QWidget window;
        litecode::ui::WindowStateService service(manager, window);
        auto* interval = service.findChild<QTimer*>("windowActivityIntervals");
        auto* inactive = service.findChild<QTimer*>("windowInactiveDebounce");
        QVERIFY(interval); QVERIFY(inactive);
        const auto start = [&](quint64 id) {
            return manager.progress().accept("fixture", "Fixture", {{"operation", "start"}, {"id", QString::number(id)},
                {"options", QJsonObject{{"location", static_cast<int>(ProgressLocation::Window)}}}});
        };
        QVERIFY(start(1));
        auto* delay = service.findChild<QTimer*>("windowProgressActivityDelay"); QVERIFY(delay); QCOMPARE(delay->interval(), 15'000);
        QVERIFY(QMetaObject::invokeMethod(delay, "timeout", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(interval, "timeout", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(interval, "timeout", Qt::DirectConnection));
        QVERIFY(!inactive->isActive()); QVERIFY(manager.windowState().active);
        manager.progress().removeOwner("fixture"); QVERIFY(inactive->isActive());
        QVERIFY(QMetaObject::invokeMethod(inactive, "timeout", Qt::DirectConnection)); QVERIFY(!manager.windowState().active);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(start(2));
        delay = service.findChild<QTimer*>("windowProgressActivityDelay"); QVERIFY(delay);
        QVERIFY(QMetaObject::invokeMethod(delay, "timeout", Qt::DirectConnection));
        QVERIFY(!manager.windowState().active); manager.progress().removeOwner("fixture");
    }
};
QTEST_MAIN(WindowStateTests)
#include "tst_extension_window_state.moc"
