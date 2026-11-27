#include "extensions/ConfigurationModel.h"

#include <QDir>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <stdexcept>

namespace litecode::extensions {
namespace {

QJsonValue merge(const QJsonValue& lower, const QJsonValue& higher) {
    if (higher.isUndefined())
        return lower;
    if (!lower.isObject() || !higher.isObject())
        return higher;
    auto result = lower.toObject();
    const auto incoming = higher.toObject();
    for (auto it = incoming.begin(); it != incoming.end(); ++it)
        result.insert(it.key(), merge(result.value(it.key()), it.value()));
    return result;
}

QJsonValue lookup(QJsonValue value, const QString& section) {
    if (section.isEmpty())
        return value;
    for (const auto& segment : section.split('.')) {
        if (!value.isObject())
            return QJsonValue::Undefined;
        value = value.toObject().value(segment);
    }
    return value;
}

void insert(QJsonObject& tree, const QStringList& segments, qsizetype offset,
            const QJsonValue& value) {
    const auto& key = segments.at(offset);
    if (offset + 1 == segments.size()) {
        tree.insert(key, value);
        return;
    }
    const auto existing = tree.value(key);
    if (!existing.isUndefined() && !existing.isObject())
        throw std::invalid_argument("Configuration key conflicts with a parent value.");
    auto child = existing.toObject();
    insert(child, segments, offset + 1, value);
    tree.insert(key, child);
}

void appendUnique(QStringList& target, const QStringList& values) {
    for (const auto& value : values)
        if (!target.contains(value))
            target.append(value);
}

bool related(const QString& first, const QString& second) {
    return first.isEmpty() || second.isEmpty() || first == second ||
           first.startsWith(second + '.') || second.startsWith(first + '.');
}

bool layerChanged(const ConfigurationLayer& previous, const ConfigurationLayer& current,
                  const QString& section) {
    if (previous.value(section) != current.value(section))
        return true;
    // Keys matter when inspecting a parent object setting's nested properties:
    // those properties are not individually declared configuration settings.
    if (previous.keys != current.keys) {
        for (const auto& key : previous.keys)
            if (!current.keys.contains(key) && related(section, key))
                return true;
        for (const auto& key : current.keys)
            if (!previous.keys.contains(key) && related(section, key))
                return true;
    }
    // Group identity/order also matters: after layers merge, a single-language
    // group takes precedence over combined groups. Comparing only each layer's
    // effective language value can miss a changed grouping.
    const auto groups = [&section](const ConfigurationLayer& layer) {
        QJsonArray result;
        for (const auto& group : layer.overrides) {
            const auto value = lookup(group.contents, section);
            if (value.isUndefined())
                continue;
            result.append(QJsonObject{
                {"identifiers", QJsonArray::fromStringList(group.identifiers)}, {"value", value}});
        }
        return result;
    };
    return groups(previous) != groups(current);
}

QString normalizedPath(const QUrl& uri) {
    auto path = uri.isLocalFile() ? QDir::cleanPath(uri.toLocalFile())
                                  : uri.adjusted(QUrl::NormalizePathSegments).path();
    path.replace('\\', '/');
#ifdef Q_OS_WIN
    if (uri.isLocalFile())
        path = path.toCaseFolded();
#endif
    while (path.size() > 1 && path.endsWith('/'))
        path.chop(1);
    return path;
}

} // namespace

QStringList configurationOverrideIdentifiers(const QString& key) {
    static const QRegularExpression complete("^(\\[[^\\]]+\\])+$");
    static const QRegularExpression identifier("\\[([^\\]]+)\\]");
    if (!complete.match(key).hasMatch())
        return {};
    QStringList result;
    auto matches = identifier.globalMatch(key);
    while (matches.hasNext()) {
        const auto value = matches.next().captured(1).trimmed();
        if (!value.isEmpty() && !result.contains(value))
            result.append(value);
    }
    return result;
}

void ConfigurationLayer::addSetting(const QString& key, const QJsonValue& value) {
    const auto segments = key.split('.');
    if (key.isEmpty() || segments.contains(QString{}) || value.isUndefined())
        throw std::invalid_argument("Invalid configuration setting.");
    // Commit after validation; a conflicting dotted key must leave no partial
    // tree nodes or key registrations behind.
    auto updated = contents;
    insert(updated, segments, 0, value);
    contents = std::move(updated);
    if (!keys.contains(key))
        keys.append(key);
}

void ConfigurationLayer::addOverride(QStringList identifiers, const QJsonObject& settings) {
    if (identifiers.isEmpty() || identifiers.contains(QString{}))
        throw std::invalid_argument("Invalid configuration override identifiers.");
    identifiers.removeDuplicates();
    ConfigurationLayer tree;
    for (auto it = settings.begin(); it != settings.end(); ++it)
        tree.addSetting(it.key(), it.value());
    for (auto& existing : overrides) {
        if (existing.identifiers == identifiers) {
            existing.contents = merge(existing.contents, tree.contents).toObject();
            appendUnique(existing.keys, tree.keys);
            return;
        }
    }
    overrides.append({std::move(identifiers), std::move(tree.contents), std::move(tree.keys)});
}

QJsonValue ConfigurationLayer::value(const QString& section) const {
    return lookup(contents, section);
}

QJsonValue ConfigurationLayer::languageValue(const QString& section,
                                             const QString& languageId) const {
    QJsonValue result{QJsonValue::Undefined};
    QJsonValue individual{QJsonValue::Undefined};
    for (const auto& group : overrides) {
        if (!group.identifiers.contains(languageId))
            continue;
        if (group.identifiers.size() == 1)
            individual = merge(individual, group.contents);
        else
            result = merge(result, group.contents);
    }
    // Upstream puts the individual [language] group after combined groups,
    // independently of where it appeared in the settings document.
    return lookup(merge(result, individual), section);
}

QStringList ConfigurationLayer::languageIds(const QString& section) const {
    QStringList result;
    for (const auto& group : overrides)
        if (!lookup(group.contents, section).isUndefined())
            appendUnique(result, group.identifiers);
    return result;
}

ConfigurationLayer ConfigurationLayer::merged(const ConfigurationLayer& other) const {
    auto result = *this;
    result.contents = merge(contents, other.contents).toObject();
    appendUnique(result.keys, other.keys);
    for (const auto& incoming : other.overrides) {
        bool found = false;
        for (auto& existing : result.overrides) {
            if (existing.identifiers != incoming.identifiers)
                continue;
            existing.contents = merge(existing.contents, incoming.contents).toObject();
            appendUnique(existing.keys, incoming.keys);
            found = true;
            break;
        }
        if (!found)
            result.overrides.append(incoming);
    }
    return result;
}

std::optional<int> ConfigurationModel::folderFor(const QUrl& resource) const {
    if (!resource.isValid() || resource.isRelative())
        return std::nullopt;
    const auto path = normalizedPath(resource);
    qsizetype bestLength = -1;
    std::optional<int> result;
    for (int index = 0; index < folders.size(); ++index) {
        const auto& uri = folders.at(index).uri;
        if (!uri.isValid() || uri.isRelative())
            continue;
        if (uri.scheme() != resource.scheme() || uri.authority() != resource.authority())
            continue;
        const auto folderPath = normalizedPath(uri);
        const auto prefix = folderPath.endsWith('/') ? folderPath : folderPath + '/';
        if ((path == folderPath || path.startsWith(prefix)) && folderPath.size() > bestLength) {
            bestLength = folderPath.size();
            result = index;
        }
    }
    return result;
}

std::array<const ConfigurationLayer*, 4>
ConfigurationModel::layers(const ConfigurationResource& resource) const {
    const auto folder = resource.uri ? folderFor(*resource.uri) : std::nullopt;
    return {&defaults, &global, &workspace, folder ? &folders.at(*folder).settings : nullptr};
}

QJsonValue ConfigurationModel::get(const QString& section,
                                   const ConfigurationResource& resource) const {
    ConfigurationLayer consolidated;
    for (const auto* layer : layers(resource))
        if (layer)
            consolidated = consolidated.merged(*layer);
    QJsonValue contents = consolidated.contents;
    if (resource.languageId)
        contents = merge(contents, consolidated.languageValue({}, *resource.languageId));
    return lookup(contents, section);
}

bool ConfigurationModel::has(const QString& section, const ConfigurationResource& resource) const {
    return !get(section, resource).isUndefined();
}

std::optional<ConfigurationInspection>
ConfigurationModel::inspect(const QString& section, const ConfigurationResource& resource) const {
    if (section.isEmpty())
        return std::nullopt;
    const auto selected = layers(resource);
    bool declared = false;
    for (const auto* layer : selected) {
        if (!layer)
            continue;
        declared = declared || layer->keys.contains(section);
        for (const auto& group : layer->overrides)
            declared = declared || group.keys.contains(section);
    }
    if (!declared)
        return std::nullopt;
    ConfigurationInspection result;
    result.key = section;
    const std::array<QJsonValue*, 4> values{&result.defaultValue, &result.globalValue,
                                            &result.workspaceValue, &result.workspaceFolderValue};
    const std::array<QJsonValue*, 4> languages{
        &result.defaultLanguageValue, &result.globalLanguageValue, &result.workspaceLanguageValue,
        &result.workspaceFolderLanguageValue};
    for (std::size_t index = 0; index < selected.size(); ++index) {
        if (!selected[index])
            continue;
        *values[index] = selected[index]->value(section);
        if (resource.languageId)
            *languages[index] = selected[index]->languageValue(section, *resource.languageId);
        appendUnique(result.languageIds, selected[index]->languageIds(section));
    }
    return result;
}

bool ConfigurationModel::affectsConfiguration(
    const ConfigurationModel& previous, const QString& section,
    const std::optional<ConfigurationResource>& resource) const {
    bool changed = layerChanged(previous.defaults, defaults, section) ||
                   layerChanged(previous.global, global, section) ||
                   layerChanged(previous.workspace, workspace, section);
    QSet<QUrl> folderUris;
    for (const auto& folder : previous.folders)
        folderUris.insert(folder.uri);
    for (const auto& folder : folders)
        folderUris.insert(folder.uri);
    const ConfigurationLayer empty;
    for (const auto& uri : folderUris) {
        const ConfigurationLayer* before = &empty;
        const ConfigurationLayer* after = &empty;
        for (const auto& folder : previous.folders)
            if (folder.uri == uri) {
                before = &folder.settings;
                break;
            }
        for (const auto& folder : folders)
            if (folder.uri == uri) {
                after = &folder.settings;
                break;
            }
        changed = changed || layerChanged(*before, *after, section);
    }
    if (!changed)
        return false;
    return !resource || previous.get(section, *resource) != get(section, *resource);
}

} // namespace litecode::extensions
