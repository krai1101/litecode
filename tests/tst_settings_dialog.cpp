#include "core/DocumentTypes.h"
#include "core/SettingsService.h"
#include "ui/MainWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"
#include "ui/settings/SettingsModel.h"

#include "workspace/WorkspaceReplace.h"
#include "workspace/WorkspaceSearch.h"
#include "workspace/WorkspaceService.h"
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace {

QWidget* settingRow(litecode::ui::SettingsDialog& dialog, const QString& id) {
    for (QWidget* widget : dialog.findChildren<QWidget*>()) {
        if (widget->property("settingId").toString() == id)
            return widget;
    }
    return nullptr;
}

litecode::ui::components::ComboBox* settingsCombo(QComboBox* combo) {
    return dynamic_cast<litecode::ui::components::ComboBox*>(combo);
}

QListView* settingsPopupView(QComboBox* combo) {
    auto* shared = settingsCombo(combo);
    return shared ? shared->popupWidget()->findChild<QListView*>() : nullptr;
}

QFont editorFont(int pixelSize = 13) {
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(pixelSize);
    return font;
}

} // namespace

class SettingsDialogTest final : public QObject {
    Q_OBJECT

  private slots:
    void catalogHasStableUniqueIds();
    void buildsNavigationAndControlsFromCatalog();
    void searchFiltersRowsAndHandlesNoResults();
    void changesAreReportedImmediately();
    void savedFontWeightIsSelectedWhenDialogReopens();
    void popupSelectionRemainsVisibleWhenClosed();
    void mainWindowKeepsSelectedFontLabels();
    void closingSettingsKeepsAppliedChanges();
    void settingsCloseOnOutsideClickAndHeaderDrags();
    void settingsDescriptionCanBeSelectedAndCopied();
    void navigationSwitchesPages();
    void controlsUseSharedThemeRoles();
    void comboPopupDismissesOnOutsideClick();
    void resetControlReportsSettingAndRefreshDoesNotSave();
    void settingsAreGlobalAndDescriptionsAreSelectable();
    void topLevelMenusShareWidthAndKeepEncodingInStatusBar();
    void externalUserSettingRefreshesOpenDialog();
    void workspaceSearchInvalidatesAndReportsIncompleteResults();
    void workspaceSearchCapsBroadQueries();
};

void SettingsDialogTest::catalogHasStableUniqueIds() {
    QSet<QString> ids;
    for (const auto& category : litecode::ui::settings::categories()) {
        QVERIFY(!category.id.isEmpty());
        QVERIFY(!category.title.isEmpty());
        for (const auto& section : category.sections) {
            QVERIFY(!section.id.isEmpty());
            QVERIFY(!section.title.isEmpty());
            for (const auto& setting : section.settings) {
                QVERIFY(!setting.id.isEmpty());
                QVERIFY(!ids.contains(setting.id));
                ids.insert(setting.id);
            }
        }
    }
    QCOMPARE(ids.size(), 5);
}

void SettingsDialogTest::buildsNavigationAndControlsFromCatalog() {
    litecode::ui::SettingsDialog dialog(editorFont(), true,
                                        litecode::core::TextEncoding::Windows1252);
    QVERIFY(!dialog.isModal());
    QCOMPARE(dialog.windowModality(), Qt::NonModal);
    auto* navigation = dialog.findChild<QListWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(navigation);
    QCOMPARE(navigation->count(), 2);
    QCOMPARE(navigation->item(0)->text(), QStringLiteral("Font"));
    QCOMPARE(navigation->item(1)->text(), QStringLiteral("Files"));

    auto* encoding = dialog.findChild<QComboBox*>(QStringLiteral("fileEncoding"));
    QVERIFY(encoding);
    QCOMPARE(encoding->count(), litecode::core::supportedTextEncodings().size());
    QCOMPARE(encoding->currentData().toString(), QStringLiteral("windows1252"));
    QCOMPARE(encoding->minimumWidth(), 400);
    QCOMPARE(encoding->maximumWidth(), 400);
    QVERIFY(settingsCombo(encoding));
    QVERIFY(settingsPopupView(encoding));
    QVERIFY(settingRow(dialog, QStringLiteral("editor.fontFamily")));
    QVERIFY(settingRow(dialog, QStringLiteral("editor.fontWeight")));
    QVERIFY(settingRow(dialog, QStringLiteral("editor.fontSize")));
    QVERIFY(settingRow(dialog, QStringLiteral("files.autoGuessEncoding")));
    QVERIFY(settingRow(dialog, QStringLiteral("files.encoding")));
    auto* close = dialog.findChild<QToolButton*>(QStringLiteral("dialogClose"));
    QVERIFY(close);
    QCOMPARE(close->text(), QStringLiteral("×"));
    dialog.show();
    QCoreApplication::processEvents();
    QVERIFY(close->isVisibleTo(&dialog));
    const QPoint closePosition = close->mapTo(&dialog, QPoint());
    QVERIFY(closePosition.x() >= dialog.width() - close->width() - 8);
    auto* fontFamily = dialog.findChild<QComboBox*>(QStringLiteral("editorFontFamily"));
    auto* fontWeight = dialog.findChild<QComboBox*>(QStringLiteral("editorFontWeight"));
    auto* fontSize = dialog.findChild<QSpinBox*>(QStringLiteral("editorFontSize"));
    QVERIFY(fontFamily);
    QVERIFY(fontWeight);
    QVERIFY(fontSize);
    QCOMPARE(fontFamily->currentText(), QStringLiteral("Consolas"));
    QCOMPARE(fontWeight->currentData().toInt(), QFont::Normal);
    QCOMPARE(fontSize->value(), 13);
}

void SettingsDialogTest::searchFiltersRowsAndHandlesNoResults() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    dialog.resize(1000, 650);
    dialog.show();
    QTest::qWait(1);
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    QVERIFY(search);

    search->setText(QStringLiteral("encoding"));
    QCoreApplication::processEvents();
    QVERIFY(settingRow(dialog, QStringLiteral("editor.fontFamily"))->isHidden());
    QVERIFY(settingRow(dialog, QStringLiteral("editor.fontSize"))->isHidden());
    QVERIFY(!settingRow(dialog, QStringLiteral("files.autoGuessEncoding"))->isHidden());
    QVERIFY(!settingRow(dialog, QStringLiteral("files.encoding"))->isHidden());
    const auto scrollAreas =
        dialog.findChildren<QScrollArea*>(QStringLiteral("settingsScrollArea"));
    QCOMPARE(scrollAreas.size(), 2);
    for (QScrollArea* scrollArea : scrollAreas)
        QCOMPARE(scrollArea->horizontalScrollBar()->maximum(), 0);

    search->setText(QStringLiteral("definitely-not-a-setting"));
    QCoreApplication::processEvents();
    const auto labels = dialog.findChildren<QLabel*>();
    QLabel* empty = nullptr;
    for (QLabel* label : labels) {
        if (label->property("uiRole").toString() == QStringLiteral("emptyState")) {
            empty = label;
            break;
        }
    }
    QVERIFY(empty);
    QVERIFY(!empty->isHidden());
}

void SettingsDialogTest::changesAreReportedImmediately() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    QSignalSpy changes(&dialog, &litecode::ui::SettingsDialog::valuesChanged);
    QSignalSpy settingChanges(&dialog, &litecode::ui::SettingsDialog::settingChanged);
    auto* autoGuess = dialog.findChild<QCheckBox*>();
    auto* encoding = dialog.findChild<QComboBox*>(QStringLiteral("fileEncoding"));
    auto* fontWeight = dialog.findChild<QComboBox*>(QStringLiteral("editorFontWeight"));
    auto* fontSize = dialog.findChild<QSpinBox*>(QStringLiteral("editorFontSize"));
    QVERIFY(autoGuess);
    QVERIFY(encoding);
    QVERIFY(fontWeight);
    QVERIFY(fontSize);

    autoGuess->setChecked(true);
    QCOMPARE(changes.count(), 1);
    encoding->setCurrentIndex(encoding->currentIndex() + 1);
    QCOMPARE(changes.count(), 2);
    fontWeight->setCurrentIndex(fontWeight->findData(static_cast<int>(QFont::Bold)));
    QCOMPARE(changes.count(), 3);
    QCOMPARE(dialog.editorFont().weight(), QFont::Bold);
    fontSize->setValue(14);
    QCOMPARE(changes.count(), 4);
    QCOMPARE(dialog.editorFont().pixelSize(), 14);
    QCOMPARE(settingChanges.count(), 4);
    QVERIFY(dialog.hasPendingChanges());
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("settingsSaveButton")));
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("settingsCancelButton")));
}

void SettingsDialogTest::savedFontWeightIsSelectedWhenDialogReopens() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profile = directory.filePath(QStringLiteral("settings.ini"));
    {
        litecode::core::SettingsService settings(profile);
        settings.saveEditorFontWeight(QFont::Bold);
        QVERIFY(settings.sync());
    }

    litecode::core::SettingsService reopened(profile);
    QFont font(editorFont());
    font.setWeight(static_cast<QFont::Weight>(reopened.editorFontWeight()));
    litecode::ui::SettingsDialog dialog(font, false, litecode::core::TextEncoding::Utf8);
    auto* weight = dialog.findChild<QComboBox*>(QStringLiteral("editorFontWeight"));
    QVERIFY(weight);
    dialog.show();
    QCoreApplication::processEvents();
    QCOMPARE(weight->currentText(), QStringLiteral("Bold"));
    QCOMPARE(weight->currentData().toInt(), QFont::Bold);
    weight->setCurrentIndex(0);
    dialog.setValues(font, false, litecode::core::TextEncoding::Utf8);
    QCOMPARE(weight->currentText(), QStringLiteral("Bold"));
}

void SettingsDialogTest::popupSelectionRemainsVisibleWhenClosed() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    dialog.show();
    QCoreApplication::processEvents();
    auto* weight = dialog.findChild<QComboBox*>(QStringLiteral("editorFontWeight"));
    auto* family = dialog.findChild<QComboBox*>(QStringLiteral("editorFontFamily"));
    QVERIFY(weight);
    QVERIFY(family);
    QVERIFY(!family->isEditable());
    QCOMPARE(family->cursor().shape(), Qt::PointingHandCursor);
    QCOMPARE(weight->cursor().shape(), Qt::PointingHandCursor);

    QTest::mouseClick(weight, Qt::LeftButton, Qt::NoModifier, QPoint(16, weight->height() / 2));
    auto* weightView = settingsPopupView(weight);
    QVERIFY(weightView);
    QTRY_VERIFY(weightView->isVisible());
    const int boldIndex = weight->findText(QStringLiteral("Bold"));
    QVERIFY(boldIndex >= 0);
    QTest::keyClick(weightView, Qt::Key_End);
    QTest::keyClick(weightView, Qt::Key_Return);
    QTRY_VERIFY(!weightView->isVisible());
    QCOMPARE(weight->currentText(), QStringLiteral("Bold"));

    QTest::mouseClick(family, Qt::LeftButton, Qt::NoModifier, QPoint(16, family->height() / 2));
    auto* familyView = settingsPopupView(family);
    QVERIFY(familyView);
    QTRY_VERIFY(familyView->isVisible());
    QVERIFY(family->count() > 1);
    const int targetIndex = family->currentIndex() == 0 ? 1 : 0;
    const QString targetFamily = family->itemText(targetIndex);
    QTest::keyClick(familyView, Qt::Key_Home);
    if (targetIndex == 1)
        QTest::keyClick(familyView, Qt::Key_Down);
    QTest::keyClick(familyView, Qt::Key_Return);
    QTRY_VERIFY(!familyView->isVisible());
    QCOMPARE(family->currentText(), targetFamily);
}

void SettingsDialogTest::mainWindowKeepsSelectedFontLabels() {
    litecode::ui::Theme::apply(*qApp, true);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    litecode::core::SettingsService settings(directory.filePath(QStringLiteral("user.ini")));
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch search;
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, false);
    window.show();
    auto* settingsAction = window.findChild<QAction*>(QStringLiteral("preferences.settings"));
    QVERIFY(settingsAction);
    settingsAction->trigger();
    auto* dialog = window.findChild<litecode::ui::SettingsDialog*>();
    QVERIFY(dialog);
    auto* weight = dialog->findChild<QComboBox*>(QStringLiteral("editorFontWeight"));
    QVERIFY(weight);
    weight->showPopup();
    auto* weightView = settingsPopupView(weight);
    QVERIFY(weightView);
    QTRY_VERIFY(weightView->isVisible());
    const int boldIndex = weight->findText(QStringLiteral("Bold"));
    QVERIFY(boldIndex >= 0);
    const QRect boldRow = weightView->visualRect(weight->model()->index(boldIndex, 0));
    QTest::mouseClick(weightView->viewport(), Qt::LeftButton, Qt::NoModifier, boldRow.center());
    QTRY_VERIFY(!weightView->isVisible());
    QCOMPARE(settings.editorFontWeight(), static_cast<int>(QFont::Bold));
    QCOMPARE(weight->currentText(), QStringLiteral("Bold"));
    QCoreApplication::processEvents();
    QCOMPARE(settings.editorFontWeight(), static_cast<int>(QFont::Bold));
    QCOMPARE(weight->currentText(), QStringLiteral("Bold"));

    auto* family = dialog->findChild<QComboBox*>(QStringLiteral("editorFontFamily"));
    QVERIFY(family);
    family->showPopup();
    auto* familyView = settingsPopupView(family);
    QVERIFY(familyView);
    QTRY_VERIFY(familyView->isVisible());
    QVERIFY(family->count() > 1);
    const int targetIndex = family->currentIndex() == 0 ? 1 : 0;
    const QString targetFamily = family->itemText(targetIndex);
    familyView->scrollTo(family->model()->index(targetIndex, 0));
    QCoreApplication::processEvents();
    const QRect targetRow = familyView->visualRect(family->model()->index(targetIndex, 0));
    QTest::mouseClick(familyView->viewport(), Qt::LeftButton, Qt::NoModifier, targetRow.center());
    QTRY_VERIFY(!familyView->isVisible());
    QCOMPARE(settings.editorFontFamily(), targetFamily);
    QCOMPARE(family->currentText(), targetFamily);
    QVERIFY(!family->isEditable());
    QVERIFY(!dialog->findChild<QPushButton*>(QStringLiteral("settingsSaveButton")));
    QCOMPARE(settings.editorFontWeight(), static_cast<int>(QFont::Bold));
    QCOMPARE(settings.editorFontFamily(), targetFamily);
}

void SettingsDialogTest::closingSettingsKeepsAppliedChanges() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    litecode::core::SettingsService settings(directory.filePath(QStringLiteral("user.ini")));
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch search;
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, false);
    window.show();
    auto* settingsAction = window.findChild<QAction*>(QStringLiteral("preferences.settings"));
    QVERIFY(settingsAction);
    settingsAction->trigger();
    auto* dialog = window.findChild<litecode::ui::SettingsDialog*>();
    QVERIFY(dialog);
    auto* weight = dialog->findChild<QComboBox*>(QStringLiteral("editorFontWeight"));
    QVERIFY(weight);
    weight->setCurrentIndex(weight->findData(static_cast<int>(QFont::Bold)));
    QVERIFY(!dialog->hasPendingChanges());
    QCOMPARE(settings.editorFontWeight(), static_cast<int>(QFont::Bold));
    dialog->close();
    QCOMPARE(settings.editorFontWeight(), static_cast<int>(QFont::Bold));
}

void SettingsDialogTest::settingsCloseOnOutsideClickAndHeaderDrags() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    litecode::core::SettingsService settings(directory.filePath(QStringLiteral("user.ini")));
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch search;
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, false);
    window.show();
    auto* settingsAction = window.findChild<QAction*>(QStringLiteral("preferences.settings"));
    QVERIFY(settingsAction);
    settingsAction->trigger();
    QPointer<litecode::ui::SettingsDialog> dialog =
        window.findChild<litecode::ui::SettingsDialog*>();
    QVERIFY(dialog);
    auto* header = dialog->findChild<QWidget*>(QStringLiteral("dialogHeader"));
    QVERIFY(header);
    QCOMPARE(header->cursor().shape(), Qt::ArrowCursor);

    const QPoint originalPosition = dialog->pos();
    const QPoint localStart(50, header->height() / 2);
    const QPoint globalStart = header->mapToGlobal(localStart);
    QMouseEvent press(QEvent::MouseButtonPress, localStart, globalStart, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(header, &press);
    QCOMPARE(header->cursor().shape(), Qt::ArrowCursor);
    QMouseEvent move(QEvent::MouseMove, localStart, globalStart + QPoint(30, 20), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(header, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, localStart, globalStart + QPoint(30, 20),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(header, &release);
    QCOMPARE(header->cursor().shape(), Qt::ArrowCursor);
    QCOMPARE(dialog->pos(), originalPosition);

    QTest::mouseClick(&window, Qt::LeftButton);
    QTRY_VERIFY(!dialog);
}

void SettingsDialogTest::settingsDescriptionCanBeSelectedAndCopied() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    dialog.show();
    QCoreApplication::processEvents();
    QWidget* row = settingRow(dialog, QStringLiteral("editor.fontWeight"));
    QVERIFY(row);
    QLabel* description = nullptr;
    for (QLabel* label : row->findChildren<QLabel*>()) {
        if (label->property("uiRole") == QStringLiteral("description")) {
            description = label;
            break;
        }
    }
    QVERIFY(description);
    const QPoint start(2, description->height() / 2);
    const QPoint finish(90, description->height() / 2);
    QMouseEvent press(QEvent::MouseButtonPress, start, description->mapToGlobal(start),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(description, &press);
    QMouseEvent move(QEvent::MouseMove, finish, description->mapToGlobal(finish), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(description, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, finish, description->mapToGlobal(finish),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(description, &release);
    QVERIFY(description->hasSelectedText());
    description->setFocus();
    qApp->clipboard()->clear();
    QTest::keyClick(description, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(qApp->clipboard()->text(), description->selectedText());
}

void SettingsDialogTest::navigationSwitchesPages() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    auto* navigation = dialog.findChild<QListWidget*>(QStringLiteral("settingsNavigation"));
    auto* pages = dialog.findChild<QStackedWidget*>(QStringLiteral("settingsPages"));
    QVERIFY(navigation);
    QVERIFY(pages);
    dialog.show();
    QCoreApplication::processEvents();
    auto* font = navigation->item(0);
    auto* files = navigation->item(1);
    QCOMPARE(navigation->currentItem(), font);
    QCOMPARE(qobject_cast<QScrollArea*>(pages->currentWidget())
                 ->widget()
                 ->property("settingsSectionKey"),
             QVariant(QStringLiteral("textEditor/font")));

    navigation->itemClicked(files);
    QCOMPARE(navigation->currentItem(), files);
    QCOMPARE(qobject_cast<QScrollArea*>(pages->currentWidget())
                 ->widget()
                 ->property("settingsSectionKey"),
             QVariant(QStringLiteral("textEditor/files")));
}

void SettingsDialogTest::controlsUseSharedThemeRoles() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    const auto inputRole = QStringLiteral("input");
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("editorFontFamily"))
                 ->property("uiComponent")
                 .toString(),
             inputRole);
    QVERIFY(dynamic_cast<litecode::ui::components::ComboBox*>(
        dialog.findChild<QComboBox*>(QStringLiteral("editorFontFamily"))));
    auto* fontFamily = dialog.findChild<QComboBox*>(QStringLiteral("editorFontFamily"));
    auto* familyView = settingsPopupView(fontFamily);
    QVERIFY(familyView);
    QCOMPARE(familyView->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QVERIFY(familyView->verticalScrollBar()->property("scrollbarActive").toBool());
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("editorFontWeight"))
                 ->property("uiComponent")
                 .toString(),
             inputRole);
    QCOMPARE(dialog.findChild<QSpinBox*>(QStringLiteral("editorFontSize"))
                 ->property("uiComponent")
                 .toString(),
             inputRole);
    QCOMPARE(dialog.findChild<QComboBox*>(QStringLiteral("fileEncoding"))
                 ->property("uiComponent")
                 .toString(),
             inputRole);
    QCOMPARE(dialog.findChild<QCheckBox*>()->property("uiComponent").toString(),
             QStringLiteral("checkbox"));
    QCOMPARE(dialog.findChild<QListWidget*>(QStringLiteral("settingsNavigation"))
                 ->property("uiComponent")
                 .toString(),
             QStringLiteral("list"));
    QVERIFY(!litecode::ui::Theme::darkStyleSheet().contains(QLatin1Char('@')));
    QVERIFY(!litecode::ui::Theme::lightStyleSheet().contains(QLatin1Char('@')));
    for (const bool dark : {false, true}) {
        const auto tokens = litecode::ui::Theme::tokens(dark);
        QVERIFY(tokens.listSelectionBackground != tokens.listHoverBackground);
        const QIcon icon = litecode::ui::themedIcon(QStringLiteral(":/icons/clear.svg"), dark);
        QVERIFY(!icon.isNull());
        const QImage pixels = icon.pixmap(16, 16).toImage().convertToFormat(QImage::Format_ARGB32);
        bool foundGlyphPixel = false;
        for (int y = 0; y < pixels.height() && !foundGlyphPixel; ++y) {
            for (int x = 0; x < pixels.width(); ++x) {
                const QColor pixel = pixels.pixelColor(x, y);
                // SVG rasterizers can generate partially transparent antialiased edge pixels
                // with platform-dependent color channels. Verify an opaque glyph pixel instead.
                if (pixel.alpha() != 255)
                    continue;
                QVERIFY(qAbs(pixel.red() - tokens.iconForeground.red()) <= 1);
                QVERIFY(qAbs(pixel.green() - tokens.iconForeground.green()) <= 1);
                QVERIFY(qAbs(pixel.blue() - tokens.iconForeground.blue()) <= 1);
                foundGlyphPixel = true;
                break;
            }
        }
        QVERIFY(foundGlyphPixel);
    }
}

void SettingsDialogTest::comboPopupDismissesOnOutsideClick() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    dialog.show();
    QCoreApplication::processEvents();

    auto* encoding = dialog.findChild<QComboBox*>(QStringLiteral("fileEncoding"));
    auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
    auto* navigation = dialog.findChild<QListWidget*>(QStringLiteral("settingsNavigation"));
    QVERIFY(encoding);
    QVERIFY(search);
    QVERIFY(navigation);

    navigation->itemClicked(navigation->item(1));
    QCoreApplication::processEvents();

    QVERIFY(encoding->isVisibleTo(&dialog));
    QTest::mouseClick(encoding, Qt::LeftButton);
    auto* encodingView = settingsPopupView(encoding);
    QVERIFY(encodingView);
    QTRY_VERIFY(encodingView->isVisible());

    QTest::mouseClick(search, Qt::LeftButton);
    QTRY_VERIFY(!encodingView->isVisible());
}

void SettingsDialogTest::resetControlReportsSettingAndRefreshDoesNotSave() {
    litecode::ui::SettingsDialog dialog(editorFont(), true,
                                        litecode::core::TextEncoding::Windows1252);
    QSignalSpy resets(&dialog, &litecode::ui::SettingsDialog::resetRequested);
    QSignalSpy changes(&dialog, &litecode::ui::SettingsDialog::valuesChanged);
    QSignalSpy settingChanges(&dialog, &litecode::ui::SettingsDialog::settingChanged);
    QWidget* row = settingRow(dialog, QStringLiteral("files.encoding"));
    QVERIFY(row);
    auto* reset = row->findChild<QToolButton*>();
    QVERIFY(reset);
    reset->click();
    QCOMPARE(resets.count(), 1);
    QCOMPARE(resets.first().first().toString(), QStringLiteral("files.encoding"));

    dialog.setValues(editorFont(14), false, litecode::core::TextEncoding::Utf8);
    QCOMPARE(changes.count(), 0);
    QCOMPARE(dialog.fileEncoding(), litecode::core::TextEncoding::Utf8);
    QVERIFY(!dialog.autoGuessEncoding());
    QCOMPARE(dialog.editorFont().pixelSize(), 14);
    QCOMPARE(settingChanges.count(), 0);
}

void SettingsDialogTest::settingsAreGlobalAndDescriptionsAreSelectable() {
    litecode::ui::SettingsDialog dialog(editorFont(), false, litecode::core::TextEncoding::Utf8);
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("settingsWorkspaceScope")));
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("settingsUserScope")));
    QWidget* row = settingRow(dialog, QStringLiteral("files.encoding"));
    QVERIFY(row);
    const auto labels = row->findChildren<QLabel*>();
    QVERIFY(!labels.isEmpty());
    for (QLabel* label : labels) {
        QVERIFY(label->textInteractionFlags().testFlag(Qt::TextSelectableByMouse));
        QVERIFY(label->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
    }
}

void SettingsDialogTest::topLevelMenusShareWidthAndKeepEncodingInStatusBar() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    litecode::core::SettingsService settings(directory.filePath(QStringLiteral("user.ini")));
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch search;
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, false);

    auto* menuBar = window.findChild<QMenuBar*>();
    QVERIFY(menuBar);
    int width = 0;
    for (QAction* action : menuBar->actions()) {
        QMenu* menu = action->menu();
        QVERIFY(menu);
        if (width == 0)
            width = menu->minimumWidth();
        QCOMPARE(menu->minimumWidth(), width);
        QVERIFY(width >= 220);
        if (action->text() == QStringLiteral("File")) {
            for (QAction* fileAction : menu->actions()) {
                QVERIFY(!fileAction->text().contains(QStringLiteral("Encoding")));
            }
        }
    }
    QVERIFY(window.findChild<QToolButton*>(QStringLiteral("encodingStatusButton")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("file.openFileWithEncoding")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("file.changeEncoding")));
}

void SettingsDialogTest::externalUserSettingRefreshesOpenDialog() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString profile = directory.filePath(QStringLiteral("user.ini"));
    litecode::core::SettingsService settings(profile);
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch search;
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, false);

    QAction* settingsAction = nullptr;
    for (QAction* action : window.actions()) {
        if (action->text().startsWith(QStringLiteral("Settings"))) {
            settingsAction = action;
            break;
        }
    }
    QVERIFY(settingsAction);
    settingsAction->trigger();
    auto* dialog = window.findChild<litecode::ui::SettingsDialog*>();
    QVERIFY(dialog);
    auto* encoding = dialog->findChild<QComboBox*>(QStringLiteral("fileEncoding"));
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("userEncodingError"));
    QVERIFY(encoding);
    QVERIFY(warning);

    QFile external(profile);
    QVERIFY(external.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(external.write("[files]\nencoding=windows1252\n"
                           "[appearance]\ntheme=light\n"
                           "[shortcuts]\nfile.newWindow=Alt+9\n") > 0);
    external.close();
    QTRY_COMPARE_WITH_TIMEOUT(encoding->currentData().toString(), QStringLiteral("windows1252"),
                              5000);
    auto* newWindowAction = window.findChild<QAction*>(QStringLiteral("newWindowAction"));
    QVERIFY(newWindowAction);
    QTRY_COMPARE_WITH_TIMEOUT(newWindowAction->shortcut(), QKeySequence(QStringLiteral("Alt+9")),
                              5000);
    QTRY_COMPARE_WITH_TIMEOUT(qApp->property("litecodeDarkTheme").toBool(), false, 5000);

    QVERIFY(external.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(external.write("[files]\nencoding=ut8\n") > 0);
    external.close();
    QTRY_VERIFY_WITH_TIMEOUT(!warning->isHidden(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(encoding->currentData().toString(), QStringLiteral("utf8"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(newWindowAction->shortcut() != QKeySequence(QStringLiteral("Alt+9")),
                             5000);
    QTRY_COMPARE_WITH_TIMEOUT(qApp->property("litecodeDarkTheme").toBool(), true, 5000);
}

void SettingsDialogTest::workspaceSearchInvalidatesAndReportsIncompleteResults() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("matches.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("needle\nneedle\nneedle\n") > 0);
    file.close();

    litecode::core::SettingsService settings(directory.filePath(QStringLiteral("user.ini")));
    settings.saveSession(directory.path(), {}, {});
    QVERIFY(settings.sync());
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::SearchBudgets budgets;
    budgets.maximumResults = 2;
    litecode::workspace::WorkspaceSearch search(nullptr, {}, budgets,
                                                directory.filePath(QStringLiteral("missing-rg")));
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, true);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("workspaceSearchInput"));
    auto* results = window.findChild<QListWidget*>(QStringLiteral("workspaceSearchResults"));
    auto* replaceAll = window.findChild<QToolButton*>(QStringLiteral("workspaceReplaceAllButton"));
    QVERIFY(input);
    QVERIFY(results);
    QVERIFY(replaceAll);

    input->setText(QStringLiteral("needle"));
    QTRY_VERIFY_WITH_TIMEOUT(
        [results] {
            for (int row = 0; row < results->count(); ++row) {
                if (results->item(row)->text().contains(QStringLiteral("subset of all matches")))
                    return true;
            }
            return false;
        }(),
        5000);
    QVERIFY(replaceAll->isEnabled());
    auto* replacement = window.findChild<QLineEdit*>(QStringLiteral("workspaceReplaceInput"));
    QVERIFY(replacement);
    replacement->setText(QStringLiteral("new"));
    QCOMPARE(results->item(3)->data(Qt::UserRole + 2).toString(),
             QStringLiteral("workspaceSearchMatch"));
    QVERIFY(results->itemWidget(results->item(3)) == nullptr);

    input->setText(QStringLiteral("other"));
    // Keep the previous tree visible while the next search runs, but never allow
    // replacement against those now-stale matches.
    QVERIFY(!replaceAll->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(results->count(), 1, 5000);
    QCOMPARE(results->item(0)->text(), QStringLiteral("No results found"));

    // A literal query containing spaces must not silently turn into "needle".
    QSignalSpy searched(&search, &litecode::workspace::WorkspaceSearch::operationCompleted);
    input->setText(QStringLiteral(" needle "));
    QTRY_VERIFY_WITH_TIMEOUT(!searched.isEmpty(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(results->count(), 1, 5000);
    QCOMPARE(results->item(0)->text(), QStringLiteral("No results found"));
}

void SettingsDialogTest::workspaceSearchCapsBroadQueries() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("matches.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArrayLiteral("zqxneedle ").repeated(501)), qint64{5010});
    file.close();
    QFile another(directory.filePath(QStringLiteral("more.txt")));
    QVERIFY(another.open(QIODevice::WriteOnly));
    QCOMPARE(another.write(QByteArrayLiteral("zqxneedle\n")), qint64{10});
    another.close();

    litecode::core::SettingsService settings(directory.filePath(QStringLiteral("user.ini")));
    settings.saveSession(directory.path(), {}, {});
    QVERIFY(settings.sync());
    litecode::workspace::WorkspaceService workspace;
    litecode::workspace::WorkspaceSearch search(nullptr, {}, {},
                                                directory.filePath(QStringLiteral("missing-rg")));
    litecode::workspace::WorkspaceSearch quickOpen;
    litecode::workspace::WorkspaceReplace replace;
    litecode::ui::WorkbenchServices services{search, quickOpen, replace};
    litecode::ui::MainWindow window(settings, workspace, services, true);
    auto* input = window.findChild<QLineEdit*>(QStringLiteral("workspaceSearchInput"));
    auto* results = window.findChild<QListWidget*>(QStringLiteral("workspaceSearchResults"));
    QVERIFY(input);
    QVERIFY(results);
    input->setText(QStringLiteral("zqxneedle"));
    QTRY_VERIFY_WITH_TIMEOUT(
        results->count() > 500 &&
            results->item(0)->text().startsWith(QStringLiteral("502 results in 2 files")),
        10'000);
    QCOMPARE(results->item(1)->data(Qt::UserRole + 2).toString(),
             QStringLiteral("workspaceSearchWarning"));
    QVERIFY(results->item(1)->text().contains(QStringLiteral("subset of all matches")));
    QCOMPARE(results->item(250)->data(Qt::UserRole + 2).toString(),
             QStringLiteral("workspaceSearchMatch"));
    QVERIFY(results->itemWidget(results->item(250)) == nullptr);
}

QTEST_MAIN(SettingsDialogTest)
#include "tst_settings_dialog.moc"
