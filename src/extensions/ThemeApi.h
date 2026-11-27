#pragma once
#include <QString>
#include <QUrl>
#include <optional>
#include <utility>
#include <variant>

namespace litecode::extensions {
class ThemeColor final {
  public:
    explicit ThemeColor(QString id) : id_(std::move(id)) {}
    [[nodiscard]] const QString& id() const { return id_; }

  private:
    QString id_;
};
class ThemeIcon final {
  public:
    static const ThemeIcon File, Folder;
    explicit ThemeIcon(QString id, std::optional<ThemeColor> color = {})
        : id_(std::move(id)), color_(std::move(color)) {}
    [[nodiscard]] const QString& id() const { return id_; }
    [[nodiscard]] const std::optional<ThemeColor>& color() const { return color_; }

  private:
    QString id_;
    std::optional<ThemeColor> color_;
};
inline const ThemeIcon ThemeIcon::File{QStringLiteral("file")};
inline const ThemeIcon ThemeIcon::Folder{QStringLiteral("folder")};
struct LightDarkIconPath final {
    QUrl light, dark;
};
using IconPath = std::variant<QUrl, LightDarkIconPath, ThemeIcon>;
} // namespace litecode::extensions
