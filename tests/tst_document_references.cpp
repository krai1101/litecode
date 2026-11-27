#include "editor/BoundDocumentReferences.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace litecode;
class DocumentReferenceTests final : public QObject {
    Q_OBJECT
  private slots:
    void removingParentDropsOnlyItsReferences() {
        QTemporaryDir directory;
        QVector<quint64> released;
        editor::BoundDocumentReferences references(
            [&released](core::DocumentId id) { released.append(id.value()); });
        references.add(core::DocumentId(1), QUrl::fromLocalFile(directory.filePath("parent/a.txt")),
                       1);
        references.add(core::DocumentId(2),
                       QUrl::fromLocalFile(directory.filePath("parent/nested/b.txt")), 1);
        references.add(core::DocumentId(3),
                       QUrl::fromLocalFile(directory.filePath("parent-other/c.txt")), 1);
        references.remove(QUrl::fromLocalFile(directory.filePath("parent")));
        QCOMPARE(released, (QVector<quint64>{1, 2}));
        QCOMPARE(references.count(), qsizetype(1));
        QCOMPARE(references.length(), qint64(1));
    }
    void countTrimsWithUpstreamHysteresis() {
        QVector<quint64> released;
        editor::BoundDocumentReferences references(
            [&released](core::DocumentId id) { released.append(id.value()); });
        for (quint64 id = 1; id <= 59; ++id)
            references.add(core::DocumentId(id), QUrl(QString("file:///ref-%1").arg(id)), 1);
        QCOMPARE(references.count(), qsizetype(59));
        QVERIFY(released.isEmpty());
        references.add(core::DocumentId(60), QUrl("file:///ref-60"), 1);
        QCOMPARE(references.count(), qsizetype(50));
        QCOMPARE(released.size(), 10);
        QCOMPARE(released.first(), quint64(1));
        QCOMPARE(released.last(), quint64(10));
        QCOMPARE(references.length(), qint64(50));
        references.clear();
        QCOMPARE(references.length(), qint64(0));
        QCOMPARE(released.size(), 60);
    }
    void lengthBoundReleasesReferencesNotDistinctResources() {
        QVector<quint64> released;
        editor::BoundDocumentReferences references(
            [&released](core::DocumentId id) { released.append(id.value()); }, {180000, 10, 50});
        references.add(core::DocumentId(1), QUrl("file:///same"), 6);
        references.add(core::DocumentId(1), QUrl("file:///same"), 6);
        QCOMPARE(released, QVector<quint64>{1});
        QCOMPARE(references.count(), qsizetype(1));
        QCOMPARE(references.length(), qint64(6));
        references.remove(core::DocumentId(1));
        QCOMPARE(released.size(), 2);
        QVERIFY(references.count() == 0);
    }
    void expiryAndReentrantClearReleaseOnlyOnce() {
        QVector<quint64> released;
        editor::BoundDocumentReferences* active = nullptr;
        editor::BoundDocumentReferences references(
            [&](core::DocumentId id) {
                released.append(id.value());
                if (id.value() == 1)
                    active->clear();
            },
            {20, 100, 50});
        active = &references;
        references.add(core::DocumentId(1), QUrl("file:///one"), 1);
        references.add(core::DocumentId(2), QUrl("file:///two"), 1);
        QTRY_COMPARE(released.size(), 2);
        QCOMPARE(references.count(), qsizetype(0));
        QCOMPARE(references.length(), qint64(0));
        references.clear();
        QCOMPARE(released.size(), 2);
    }
};
QTEST_GUILESS_MAIN(DocumentReferenceTests)
#include "tst_document_references.moc"
