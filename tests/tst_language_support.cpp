#include "editor/LanguageStyleCatalog.h"
#include "editor/LanguageSupport.h"

#include <QTest>

using litecode::editor::languageSupportForFile;

class LanguageSupportTest final : public QObject {
    Q_OBJECT

  private slots:
    void markdownWrapsByDefault() {
        const auto language = languageSupportForFile(QStringLiteral("README.md"));

        QCOMPARE(language.id, QStringLiteral("markdown"));
        QVERIFY(language.wordWrapByDefault);
    }

    void sourceCodeDoesNotWrapByDefault() {
        const auto language = languageSupportForFile(QStringLiteral("main.cpp"));

        QCOMPARE(language.id, QStringLiteral("cpp"));
        QVERIFY(!language.wordWrapByDefault);
    }

    void cppCatalogUsesLexillaWordLists() {
        const auto keywordSets =
            litecode::editor::LanguageStyleCatalog::keywordSets(QStringLiteral("cpp"));
        const auto wordsFor = [&keywordSets](int index) {
            for (const auto& set : keywordSets) {
                if (set.index == index)
                    return set.words;
            }
            return QByteArray{};
        };

        QVERIFY(wordsFor(0).contains("return"));
        QVERIFY(wordsFor(1).contains("class"));
        QVERIFY(wordsFor(1).contains("constexpr"));
        QVERIFY(wordsFor(2).contains("param"));
        QVERIFY(wordsFor(3).contains("QCoreApplication"));
        QVERIFY(wordsFor(5).contains("TODO"));
    }

    void nativeLexersReceiveKeywordLists() {
        const auto contains = [](const QString& language, int index, const QByteArray& word) {
            const auto sets = litecode::editor::LanguageStyleCatalog::keywordSets(language);
            for (const auto& set : sets) {
                if (set.index == index)
                    return set.words.split(' ').contains(word);
            }
            return false;
        };

        QVERIFY(contains(QStringLiteral("python"), 1, QByteArrayLiteral("print")));
        QVERIFY(contains(QStringLiteral("rust"), 1, QByteArrayLiteral("u32")));
        QVERIFY(contains(QStringLiteral("shellscript"), 0, QByteArrayLiteral("if")));
        QVERIFY(contains(QStringLiteral("sql"), 0, QByteArrayLiteral("select")));
        QVERIFY(contains(QStringLiteral("cmake"), 0, QByteArrayLiteral("add_executable")));
        QVERIFY(contains(QStringLiteral("powershell"), 0, QByteArrayLiteral("function")));
        QVERIFY(contains(QStringLiteral("lua"), 1, QByteArrayLiteral("print")));
    }

    void jsonWithCommentsKeepsDistinctLanguageId() {
        const auto language =
            litecode::editor::languageSupportForFile(QStringLiteral("settings.jsonc"));
        QCOMPARE(language.id, QStringLiteral("jsonc"));
        QCOMPARE(language.lexer, QByteArrayLiteral("json"));
    }
};

QTEST_MAIN(LanguageSupportTest)
#include "tst_language_support.moc"
