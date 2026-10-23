#include "ui/settings/SettingsModel.h"

#include <QCoreApplication>

namespace litecode::ui::settings {

const QVector<CategoryDefinition>& categories() {
    static const QVector<CategoryDefinition> catalog{
        {QStringLiteral("textEditor"),
         QCoreApplication::translate("Settings", "Text Editor"),
         {{QStringLiteral("font"),
           QCoreApplication::translate("Settings", "Font"),
           {{QStringLiteral("editor.fontFamily"),
             QCoreApplication::translate("Settings", "Font Family"),
             QCoreApplication::translate("Settings",
                                         "Controls the font family used by text editors."),
             {QStringLiteral("editor"), QStringLiteral("font"), QStringLiteral("family"),
              QStringLiteral("typeface"), QStringLiteral("typography")},
             EditorKind::FontFamily},
            {QStringLiteral("editor.fontWeight"),
             QCoreApplication::translate("Settings", "Font Weight"),
             QCoreApplication::translate(
                 "Settings", "Controls the weight used for regular text in the editor."),
             {QStringLiteral("editor"), QStringLiteral("font"), QStringLiteral("weight"),
              QStringLiteral("bold"), QStringLiteral("typography")},
             EditorKind::FontWeight},
            {QStringLiteral("editor.fontSize"),
             QCoreApplication::translate("Settings", "Font Size"),
             QCoreApplication::translate("Settings", "Controls the editor font size in pixels."),
             {QStringLiteral("editor"), QStringLiteral("font"), QStringLiteral("size"),
              QStringLiteral("points"), QStringLiteral("typography")},
             EditorKind::FontSize}}},
          {QStringLiteral("files"),
           QCoreApplication::translate("Settings", "Files"),
           {{QStringLiteral("files.autoGuessEncoding"),
             QCoreApplication::translate("Settings", "Auto Guess Encoding"),
             QCoreApplication::translate(
                 "Settings",
                 "When enabled, LiteCode attempts to guess the character encoding when opening "
                 "files. Files: Encoding is used when no reliable guess is available."),
             {QStringLiteral("files"), QStringLiteral("auto"), QStringLiteral("guess"),
              QStringLiteral("encoding"), QStringLiteral("detect"), QStringLiteral("charset")},
             EditorKind::Boolean},
            {QStringLiteral("files.encoding"),
             QCoreApplication::translate("Settings", "Encoding"),
             QCoreApplication::translate(
                 "Settings", "The default character encoding used when reading and writing files."),
             {QStringLiteral("files"), QStringLiteral("encoding"), QStringLiteral("charset"),
              QStringLiteral("utf"), QStringLiteral("code page"), QStringLiteral("default")},
             EditorKind::Encoding}}}}},
    };
    return catalog;
}

} // namespace litecode::ui::settings
