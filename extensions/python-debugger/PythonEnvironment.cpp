#include "PythonEnvironment.h"
#include <QFile>
#include <QRegularExpression>
#include <stdexcept>
namespace litecode::python {
QJsonObject pythonEnvironment(const QString& path, const QJsonObject& overrides,
                              const QProcessEnvironment& inherited) {
    QJsonObject result;
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.exists()) {
            if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly))
                throw std::runtime_error(
                    "The Python environment file cannot be read or exceeds 1 MiB.");
            auto contents = QString::fromUtf8(file.readAll());
            if (contents.startsWith(QChar(0xfeff)))
                contents.remove(0, 1);
            const auto lines = contents.split('\n');
            const QRegularExpression keyPattern("^[A-Za-z_][A-Za-z0-9_]*$");
            const QRegularExpression variables(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})");
            for (qsizetype i = 0; i < lines.size(); ++i) {
                auto line = lines[i].trimmed();
                if (line.isEmpty() || line.startsWith('#'))
                    continue;
                if (line.startsWith("export "))
                    line = line.mid(7).trimmed();
                const auto equals = line.indexOf('=');
                const auto key = line.left(equals).trimmed();
                if (equals < 1 || !keyPattern.match(key).hasMatch())
                    throw std::runtime_error(QString("Invalid Python environment entry on line %1.")
                                                 .arg(i + 1)
                                                 .toStdString());
                auto value = line.mid(equals + 1).trimmed();
                bool expand = true;
                if (value.startsWith('"') || value.startsWith('\'')) {
                    const auto quote = value[0];
                    qsizetype closing = -1;
                    while (closing < 0) {
                        for (qsizetype j = 1; j < value.size(); ++j)
                            if (value[j] == quote && (j == 0 || value[j - 1] != '\\')) {
                                closing = j;
                                break;
                            }
                        if (closing >= 0)
                            break;
                        if (++i >= lines.size())
                            throw std::runtime_error("Unclosed quote in Python environment file.");
                        value += '\n' + lines[i];
                    }
                    const auto suffix = value.mid(closing + 1).trimmed();
                    if (!suffix.isEmpty() && !suffix.startsWith('#'))
                        throw std::runtime_error(
                            "Unexpected text after a quoted Python environment value.");
                    value = value.mid(1, closing - 1);
                    expand = quote == '"';
                    if (expand)
                        value.replace("\\n", "\n")
                            .replace("\\r", "\r")
                            .replace("\\t", "\t")
                            .replace("\\\"", "\"");
                } else {
                    for (qsizetype j = 0; j < value.size(); ++j)
                        if (value[j] == '#' && (j == 0 || value[j - 1].isSpace())) {
                            value = value.left(j).trimmed();
                            break;
                        }
                }
                if (expand) {
                    QString expanded;
                    qsizetype offset = 0;
                    auto matches = variables.globalMatch(value);
                    while (matches.hasNext()) {
                        const auto match = matches.next();
                        const auto name = match.captured(1);
                        expanded += value.mid(offset, match.capturedStart() - offset);
                        expanded += overrides.contains(name) ? overrides.value(name).toString()
                                    : result.contains(name)  ? result.value(name).toString()
                                                             : inherited.value(name);
                        offset = match.capturedEnd();
                    }
                    value = expanded + value.mid(offset);
                }
                result.insert(key, value);
            }
        }
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (!it.value().isString() && !it.value().isNull())
            throw std::runtime_error("Python env values must be strings or null.");
        result.insert(it.key(), it.value());
    }
    return result;
}
} // namespace litecode::python
