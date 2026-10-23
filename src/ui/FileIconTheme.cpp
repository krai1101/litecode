#include "ui/FileIconTheme.h"

#include "ui/ThemedIcon.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>

#include <array>
#include <optional>

namespace litecode::ui {
namespace {

struct SetiTheme final {
    QString fontFamily;
    QJsonObject root;
    qreal glyphScale{1.0};
    bool available{};
};

struct SetiGlyph final {
    QString text;
    QColor color;
};

qreal percentageScale(const QString& value) {
    if (!value.endsWith(QLatin1Char('%')))
        return 1.0;
    bool valid = false;
    const qreal percentage = value.chopped(1).toDouble(&valid);
    return valid && percentage > 0.0 ? percentage / 100.0 : 1.0;
}

const SetiTheme& setiTheme() {
    static const SetiTheme theme = [] {
        SetiTheme result;
        // Qt's Windows DirectWrite backend rejects WOFF application fonts. The bundled TTF is
        // a lossless container conversion of the licensed Seti WOFF asset.
        const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/icons/seti.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (fontId < 0 || families.isEmpty()) {
            return result;
        }
        QFile mapping(QStringLiteral(":/icons/vs-seti-icon-theme.json"));
        if (!mapping.open(QIODevice::ReadOnly)) {
            return result;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(mapping.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            return result;
        }
        result.fontFamily = families.constFirst();
        result.root = document.object();
        const QJsonArray fonts = result.root.value(QStringLiteral("fonts")).toArray();
        if (!fonts.isEmpty()) {
            result.glyphScale =
                percentageScale(fonts.at(0).toObject().value(QStringLiteral("size")).toString());
        }
        result.available = true;
        return result;
    }();
    return theme;
}

QString mappedValue(const QJsonObject& primary, const QJsonObject& fallback, const QString& key) {
    const QString value = primary.value(key).toString();
    return value.isEmpty() ? fallback.value(key).toString() : value;
}

QString languageIdForFile(const QFileInfo& info) {
    const QString name = info.fileName().toLower();
    const QString suffix = info.suffix().toLower();
    if (name == QStringLiteral(".gitignore") || name == QStringLiteral(".ignore") ||
        name == QStringLiteral(".dockerignore"))
        return QStringLiteral("ignore");
    if (name == QStringLiteral("dockerfile") || name.startsWith(QStringLiteral("dockerfile.")))
        return QStringLiteral("dockerfile");
    if (name == QStringLiteral(".env") || name.startsWith(QStringLiteral(".env.")))
        return QStringLiteral("dotenv");
    if (name == QStringLiteral("cmakelists.txt") || suffix == QStringLiteral("cmake"))
        return QStringLiteral("cmake");

    // Seti assigns the primary icon for many languages through languageIds rather
    // than fileExtensions. Keep this translation in one place so an unsupported
    // language cannot silently fall through to a look-alike fallback icon.
    static const QHash<QString, QString> ids{
        {QStringLiteral("c"), QStringLiteral("c")},
        {QStringLiteral("cc"), QStringLiteral("cpp")},
        {QStringLiteral("cpp"), QStringLiteral("cpp")},
        {QStringLiteral("cxx"), QStringLiteral("cpp")},
        {QStringLiteral("hh"), QStringLiteral("cpp")},
        {QStringLiteral("hpp"), QStringLiteral("cpp")},
        {QStringLiteral("hxx"), QStringLiteral("cpp")},
        {QStringLiteral("cs"), QStringLiteral("csharp")},
        {QStringLiteral("css"), QStringLiteral("css")},
        {QStringLiteral("dart"), QStringLiteral("dart")},
        {QStringLiteral("fs"), QStringLiteral("fsharp")},
        {QStringLiteral("fsx"), QStringLiteral("fsharp")},
        {QStringLiteral("go"), QStringLiteral("go")},
        {QStringLiteral("groovy"), QStringLiteral("groovy")},
        {QStringLiteral("hbs"), QStringLiteral("handlebars")},
        {QStringLiteral("htm"), QStringLiteral("html")},
        {QStringLiteral("html"), QStringLiteral("html")},
        {QStringLiteral("java"), QStringLiteral("java")},
        {QStringLiteral("js"), QStringLiteral("javascript")},
        {QStringLiteral("cjs"), QStringLiteral("javascript")},
        {QStringLiteral("mjs"), QStringLiteral("javascript")},
        {QStringLiteral("jsx"), QStringLiteral("javascriptreact")},
        {QStringLiteral("jl"), QStringLiteral("julia")},
        {QStringLiteral("json"), QStringLiteral("json")},
        {QStringLiteral("jsonc"), QStringLiteral("jsonc")},
        {QStringLiteral("latex"), QStringLiteral("latex")},
        {QStringLiteral("less"), QStringLiteral("less")},
        {QStringLiteral("lua"), QStringLiteral("lua")},
        {QStringLiteral("md"), QStringLiteral("markdown")},
        {QStringLiteral("markdown"), QStringLiteral("markdown")},
        {QStringLiteral("php"), QStringLiteral("php")},
        {QStringLiteral("pl"), QStringLiteral("perl")},
        {QStringLiteral("pm"), QStringLiteral("perl")},
        {QStringLiteral("ps1"), QStringLiteral("powershell")},
        {QStringLiteral("py"), QStringLiteral("python")},
        {QStringLiteral("r"), QStringLiteral("r")},
        {QStringLiteral("rb"), QStringLiteral("ruby")},
        {QStringLiteral("rs"), QStringLiteral("rust")},
        {QStringLiteral("scala"), QStringLiteral("scala")},
        {QStringLiteral("sh"), QStringLiteral("shellscript")},
        {QStringLiteral("sql"), QStringLiteral("sql")},
        {QStringLiteral("swift"), QStringLiteral("swift")},
        {QStringLiteral("tex"), QStringLiteral("tex")},
        {QStringLiteral("ts"), QStringLiteral("typescript")},
        {QStringLiteral("tsx"), QStringLiteral("typescriptreact")},
        {QStringLiteral("vue"), QStringLiteral("vue")},
        {QStringLiteral("xml"), QStringLiteral("xml")},
        {QStringLiteral("yaml"), QStringLiteral("yaml")},
        {QStringLiteral("yml"), QStringLiteral("yaml")},
        {QStringLiteral("bat"), QStringLiteral("bat")},
        {QStringLiteral("cmd"), QStringLiteral("bat")},
    };
    return ids.value(suffix);
}

QString setiDefinitionForPath(const QJsonObject& root, const QFileInfo& info, bool light) {
    const QJsonObject variant =
        light ? root.value(QStringLiteral("light")).toObject() : QJsonObject{};
    const QJsonObject names = root.value(QStringLiteral("fileNames")).toObject();
    const QJsonObject variantNames = variant.value(QStringLiteral("fileNames")).toObject();
    const QJsonObject extensions = root.value(QStringLiteral("fileExtensions")).toObject();
    const QJsonObject variantExtensions =
        variant.value(QStringLiteral("fileExtensions")).toObject();
    const QJsonObject languages = root.value(QStringLiteral("languageIds")).toObject();
    const QJsonObject variantLanguages = variant.value(QStringLiteral("languageIds")).toObject();

    const QString name = info.fileName().toLower();
    QString definition = mappedValue(variantNames, names, name);
    if (!definition.isEmpty())
        return definition;

    // Seti supports compound extensions such as d.ts. Prefer the longest match.
    for (qsizetype dot = name.indexOf(QLatin1Char('.')); dot >= 0 && dot + 1 < name.size();
         dot = name.indexOf(QLatin1Char('.'), dot + 1)) {
        definition = mappedValue(variantExtensions, extensions, name.sliced(dot + 1));
        if (!definition.isEmpty())
            return definition;
    }

    const QString languageId = languageIdForFile(info);
    definition = mappedValue(variantLanguages, languages, languageId);
    if (!definition.isEmpty())
        return definition;
    return light ? variant.value(QStringLiteral("file")).toString()
                 : root.value(QStringLiteral("file")).toString();
}

std::optional<SetiGlyph> setiGlyphForDefinition(const SetiTheme& theme,
                                                const QString& definitionName) {
    if (!theme.available || definitionName.isEmpty()) {
        return std::nullopt;
    }
    const QJsonObject definitions = theme.root.value(QStringLiteral("iconDefinitions")).toObject();
    const QJsonObject definition = definitions.value(definitionName).toObject();
    QString encoded = definition.value(QStringLiteral("fontCharacter")).toString();
    if (encoded.startsWith(QLatin1Char('\\'))) {
        encoded.remove(0, 1);
    }
    bool validCharacter = false;
    const char32_t codePoint = static_cast<char32_t>(encoded.toUInt(&validCharacter, 16));
    const QColor color(definition.value(QStringLiteral("fontColor")).toString());
    if (!validCharacter || !color.isValid()) {
        return std::nullopt;
    }
    return SetiGlyph{QString::fromUcs4(&codePoint, 1), color};
}

std::optional<SetiGlyph> setiGlyphForPath(const QString& path) {
    const SetiTheme& theme = setiTheme();
    const bool light = !qApp->property("litecodeDarkTheme").toBool();
    return setiGlyphForDefinition(theme, setiDefinitionForPath(theme.root, QFileInfo(path), light));
}

QIcon renderSetiIcon(const QString& definitionName, bool light) {
    const SetiTheme& theme = setiTheme();
    if (!theme.available || definitionName.isEmpty())
        return {};

    static QHash<QString, QIcon> cache;
    const QString cacheKey =
        (light ? QStringLiteral("light:") : QStringLiteral("dark:")) + definitionName;
    if (const auto found = cache.constFind(cacheKey); found != cache.cend())
        return found.value();

    const std::optional<SetiGlyph> glyph = setiGlyphForDefinition(theme, definitionName);
    if (!glyph.has_value())
        return {};

    // Seti's 150% manifest scale is intended for a CSS icon-font line box. Rendering that
    // directly as a 20 px Qt glyph in an 18 px Explorer canvas clips several glyphs. Keep the
    // scaled appearance, but cap the ink at the available logical icon extent. Generate the
    // common device-pixel-ratio variants so Explorer does not upscale a cached 1x bitmap.
    constexpr int extent = 18;
    constexpr int iconFontBaseline = 13;
    const qreal logicalGlyphPixels = qMin<qreal>(extent - 1, iconFontBaseline * theme.glyphScale);
    QIcon icon;
    for (const qreal devicePixelRatio : std::array<qreal, 5>{1.0, 1.25, 1.5, 1.75, 2.0}) {
        const int physicalExtent = qRound(extent * devicePixelRatio);
        QPixmap pixmap(physicalExtent, physicalExtent);
        pixmap.setDevicePixelRatio(devicePixelRatio);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font(theme.fontFamily);
        font.setPixelSize(qRound(logicalGlyphPixels * devicePixelRatio));
        font.setStyleStrategy(QFont::PreferAntialias);
        font.setHintingPreference(QFont::PreferFullHinting);
        painter.setFont(font);
        painter.setPen(glyph->color);
        painter.drawText(QRect(0, 0, physicalExtent, physicalExtent), Qt::AlignCenter, glyph->text);
        icon.addPixmap(pixmap);
    }
    cache.insert(cacheKey, icon);
    return icon;
}

QIcon fallbackIconForPath(const QFileInfo& info) {
    Q_UNUSED(info);
    return themedIcon(QStringLiteral(":/icons/file.svg"),
                      qApp->property("litecodeDarkTheme").toBool());
}

} // namespace

bool setiIconThemeAvailable() { return setiTheme().available; }

QIcon fileIconForPath(const QString& path, bool directory) {
    if (directory)
        return {};
    const QFileInfo info(path);
    const SetiTheme& theme = setiTheme();
    if (theme.available) {
        const bool light = !qApp->property("litecodeDarkTheme").toBool();
        const QIcon icon = renderSetiIcon(setiDefinitionForPath(theme.root, info, light), light);
        if (!icon.isNull())
            return icon;
    }
    return fallbackIconForPath(info);
}

bool paintFileIcon(QPainter& painter, const QRect& target, const QString& path, bool directory) {
    if (directory) {
        return false;
    }
    const std::optional<SetiGlyph> glyph = setiGlyphForPath(path);
    if (!glyph.has_value()) {
        fallbackIconForPath(QFileInfo(path)).paint(&painter, target, Qt::AlignCenter);
        return false;
    }

    // This is rendered by the active viewport paint device, rather than an intermediate QIcon
    // bitmap, so Qt/DirectWrite applies the monitor's current scale and font hinting.
    // The 150% Seti manifest value is relative to VS Code's internal icon-font line-height,
    // not a literal 16 px Explorer-box multiplier. An 18 px native glyph matches its visible
    // Seti outline while retaining a small amount of vertical breathing room in the 22 px slot.
    constexpr int explorerGlyphPixels = 18;
    const SetiTheme& theme = setiTheme();
    painter.save();
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font(theme.fontFamily);
    font.setPixelSize(explorerGlyphPixels);
    font.setHintingPreference(QFont::PreferFullHinting);
    painter.setFont(font);
    painter.setPen(glyph->color);
    painter.drawText(target, Qt::AlignCenter, glyph->text);
    painter.restore();
    return true;
}

} // namespace litecode::ui
