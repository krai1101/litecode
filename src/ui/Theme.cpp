#include "ui/Theme.h"

#include "ui/components/ComponentStyle.h"
#include "ui/components/ComponentTokens.h"
#include "ui/styles/ComponentStyleSheets.h"

#include <QApplication>
#include <QFont>

namespace litecode::ui {

void Theme::apply(QApplication& application, bool dark) {
    application.setProperty("litecodeDarkTheme", dark);
    QFont interfaceFont(QStringLiteral("Segoe UI"));
    interfaceFont.setPixelSize(13);
    application.setFont(interfaceFont);
    application.setStyleSheet(dark ? darkStyleSheet() : lightStyleSheet());
}

WorkbenchPalette Theme::workbenchPalette(bool dark) {
    const ThemeTokens theme = tokens(dark);
    return {theme.sidebarSurface, theme.border};
}

ThemeTokens Theme::tokens(bool dark) {
    if (dark) {
        // VS Code Dark Modern, with neutral search focus and selection states.
        return {QColor(QStringLiteral("#181818")),
                QColor(QStringLiteral("#1f1f1f")),
                QColor(QStringLiteral("#181818")),
                QColor(QStringLiteral("#181818")),
                QColor(QStringLiteral("#202020")),
                QColor(QStringLiteral("#cccccc")),
                QColor(QStringLiteral("#9d9d9d")),
                QColor(QStringLiteral("#2b2b2b")),
                QColor(0x19, 0x1b, 0x1d, 0x4d),
                QColor(QStringLiteral("#2b2b2b")),
                QColor(0xff, 0xff, 0xff, 0x22),
                QColor(QStringLiteral("#0078d4")),
                QColor(QStringLiteral("#0078d4")),
                QColor(QStringLiteral("#f85149")),
                QColor(QStringLiteral("#e5ba7d")),
                QColor(QStringLiteral("#73c991")),
                QColor(0xa8, 0xa9, 0xaa, 0x85),
                QColor(0xa8, 0xa9, 0xaa, 0x90),
                QColor(0xa8, 0xa9, 0xaa, 0x9c),
                QColor(QStringLiteral("#313131")),
                QColor(QStringLiteral("#3c3c3c")),
                QColor(QStringLiteral("#2b2b2b")),
                QColor(QStringLiteral("#313131")),
                QColor(QStringLiteral("#555555")),
                QColor(QStringLiteral("#ffffff")),
                QColor(QStringLiteral("#c42b1c")),
                QColor(QStringLiteral("#1f1f1f")),
                QColor(QStringLiteral("#ffffff")),
                QColor(QStringLiteral("#313131")),
                QColor(QStringLiteral("#3c3c3c")),
                QColor(QStringLiteral("#1f1f1f")),
                QColor(QStringLiteral("#3c3c3c")),
                QColor(QStringLiteral("#cccccc")),
                QColor(QStringLiteral("#202020")),
                QColor(QStringLiteral("#313131")),
                QColor(QStringLiteral("#3c3c3c")),
                QColor(QStringLiteral("#cccccc")),
                QColor(QStringLiteral("#1f1f1f")),
                QColor(QStringLiteral("#454545")),
                QStringLiteral(":/icons/chevron-right-menu-dark.svg"),
                QColor(QStringLiteral("#cccccc")),
                QColor(0xff, 0xff, 0xff, 0x1a),
                QColor(0xe8, 0x11, 0x23, 0xe6),
                QColor(QStringLiteral("#6b6b6b")),
                QColor(QStringLiteral("#2a2d2e")),
                QColor(QStringLiteral("#cccccc")),
                QColor(QStringLiteral("#3d3d3d")),
                QColor(QStringLiteral("#cccccc")),
                QColor(QStringLiteral("#264f78")),
                QColor(QStringLiteral("#613214")),
                QColor(QStringLiteral("#9e6a03")),
                QColor(QStringLiteral("#454545")),
                QColor(QStringLiteral("#505050")),
                QColor(0, 0, 0, 36)};
    }
    // VS Code Light Modern.
    return {QColor(QStringLiteral("#f8f8f8")),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#f8f8f8")),
            QColor(QStringLiteral("#f8f8f8")),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#3b3b3b")),
            QColor(QStringLiteral("#767676")),
            QColor(QStringLiteral("#e5e5e5")),
            QColor(0, 0, 0, 12),
            QColor(QStringLiteral("#f2f2f2")),
            QColor(QStringLiteral("#e8e8e8")),
            QColor(QStringLiteral("#005fb8")),
            QColor(QStringLiteral("#005fb8")),
            QColor(QStringLiteral("#c42b1c")),
            QColor(QStringLiteral("#795e26")),
            QColor(QStringLiteral("#16825d")),
            QColor(0x64, 0x64, 0x64, 0x66),
            QColor(0x64, 0x64, 0x64, 0xb3),
            QColor(0x00, 0x00, 0x00, 0x99),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#cecece")),
            QColor(QStringLiteral("#dddddd")),
            QColor(QStringLiteral("#dddddd")),
            QColor(QStringLiteral("#888888")),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#c42b1c")),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#1f1f1f")),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#cecece")),
            QColor(QStringLiteral("#ffffff")),
            QColor(QStringLiteral("#cecece")),
            QColor(QStringLiteral("#3b3b3b")),
            QColor(QStringLiteral("#f8f8f8")),
            QColor(QStringLiteral("#bed6ed")),
            QColor(QStringLiteral("#005fb8")),
            QColor(QStringLiteral("#000000")),
            QColor(QStringLiteral("#fafafd")),
            QColor(QStringLiteral("#cecece")),
            QStringLiteral(":/icons/chevron-right-menu-light.svg"),
            QColor(QStringLiteral("#1e1e1e")),
            QColor(0x00, 0x00, 0x00, 0x1a),
            QColor(0xe8, 0x11, 0x23, 0xe6),
            QColor(QStringLiteral("#767676")),
            QColor(QStringLiteral("#f2f2f2")),
            QColor(QStringLiteral("#000000")),
            QColor(QStringLiteral("#e8e8e8")),
            QColor(QStringLiteral("#3b3b3b")),
            QColor(QStringLiteral("#add6ff")),
            QColor(QStringLiteral("#f8df9b")),
            QColor(QStringLiteral("#f1c15b")),
            QColor(QStringLiteral("#c9c9c9")),
            QColor(QStringLiteral("#929292")),
            QColor(0, 0, 0, 36)};
}

namespace {

QString composeStyleSheet(const ThemeTokens& tokens) {
    return components::componentStyleSheet(components::resolveComponentTokens(tokens)) +
           ComponentStyleSheets::workbench(tokens) + ComponentStyleSheets::explorer(tokens) +
           ComponentStyleSheets::editorChrome(tokens) +
           ComponentStyleSheets::terminalAndPanel(tokens) +
           ComponentStyleSheets::dialogsMenusAndTooltips(tokens) +
           ComponentStyleSheets::scrollbarsAndStatus(tokens);
}

} // namespace

QString Theme::darkStyleSheet() { return composeStyleSheet(tokens(true)); }

QString Theme::lightStyleSheet() { return composeStyleSheet(tokens(false)); }

} // namespace litecode::ui
