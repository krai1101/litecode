#include "terminal/TerminalTypes.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace litecode::terminal {
namespace {

void appendProfile(QVector<TerminalProfile>& profiles, QString id, QString displayName,
                   const QString& executable, QStringList arguments,
                   const QProcessEnvironment& environment) {
    if (executable.isEmpty() || !QFileInfo::exists(executable))
        return;
    const QString canonicalExecutable = QFileInfo(executable).canonicalFilePath();
    const bool duplicate = std::any_of(
        profiles.cbegin(), profiles.cend(), [&canonicalExecutable](const TerminalProfile& profile) {
            return QFileInfo(profile.executable).canonicalFilePath() == canonicalExecutable;
        });
    if (!duplicate) {
        profiles.push_back(
            {std::move(id), std::move(displayName), executable, std::move(arguments), environment});
    }
}

QString executablePath(const QString& name) {
    const QString path = QStandardPaths::findExecutable(name);
    return path.isEmpty() ? QString{} : QDir::cleanPath(path);
}

#ifdef Q_OS_WIN
QString gitBashPath() {
    const QString git = executablePath(QStringLiteral("git.exe"));
    if (!git.isEmpty()) {
        const QString besideGit =
            QDir(QFileInfo(git).absolutePath()).absoluteFilePath(QStringLiteral("../bin/bash.exe"));
        if (QFileInfo::exists(besideGit))
            return QDir::cleanPath(besideGit);
    }
    const QStringList candidates{
        QStringLiteral("C:/Program Files/Git/bin/bash.exe"),
        QStringLiteral("C:/Program Files (x86)/Git/bin/bash.exe"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QDir::cleanPath(candidate);
    }
    return {};
}

bool hasInstalledWslDistribution() {
    QSettings distributions(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"),
        QSettings::NativeFormat);
    return !distributions.childGroups().isEmpty();
}

QString mergedWindowsPath(const QString& freshUserPath, const QString& inheritedPath) {
    QStringList merged;
    QSet<QString> seen;
    const auto appendPath = [&merged, &seen](const QString& path) {
        for (const QString& entry : path.split(QDir::listSeparator(), Qt::SkipEmptyParts)) {
            const QString trimmed = entry.trimmed();
            if (trimmed.isEmpty())
                continue;
            const QString key = QDir::cleanPath(QDir::fromNativeSeparators(trimmed)).toCaseFolded();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            merged.append(trimmed);
        }
    };
    // Put the current user PATH first. This is where Windows installers register newly added
    // command-line tools, and it must take precedence over stale paths inherited from a parent.
    appendPath(freshUserPath);
    appendPath(inheritedPath);
    return merged.join(QDir::listSeparator());
}
#endif

} // namespace

QVector<TerminalProfile> availableTerminalProfiles() {
    QProcessEnvironment environment = refreshedProcessEnvironment();
    // Advertise the capabilities provided by VTermEngine/ConPTY. Rich terminal applications
    // otherwise assume the legacy Windows console palette and render a visibly degraded UI.
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("LiteCode"));
    environment.insert(QStringLiteral("TERM_PROGRAM_VERSION"), QStringLiteral("0.1"));
#ifndef Q_OS_WIN
    if (!environment.contains(QStringLiteral("TERM")))
        environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
#endif
    QVector<TerminalProfile> profiles;
#ifdef Q_OS_WIN
    QString commandPrompt = environment.value(QStringLiteral("ComSpec"));
    if (commandPrompt.isEmpty()) {
        commandPrompt = QDir::toNativeSeparators(QDir::cleanPath(
            environment.value(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows")) +
            QStringLiteral("/System32/cmd.exe")));
    }
    QString powerShell = executablePath(QStringLiteral("powershell.exe"));
    if (powerShell.isEmpty()) {
        powerShell = QDir::toNativeSeparators(QDir::cleanPath(
            environment.value(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows")) +
            QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe")));
    }
    appendProfile(profiles, QStringLiteral("command-prompt"), QStringLiteral("Command Prompt"),
                  commandPrompt, {QStringLiteral("/D"), QStringLiteral("/Q")}, environment);
    appendProfile(profiles, QStringLiteral("powershell"), QStringLiteral("Windows PowerShell"),
                  powerShell, {QStringLiteral("-NoLogo")}, environment);
    appendProfile(profiles, QStringLiteral("pwsh"), QStringLiteral("PowerShell"),
                  executablePath(QStringLiteral("pwsh.exe")), {QStringLiteral("-NoLogo")},
                  environment);
    appendProfile(profiles, QStringLiteral("git-bash"), QStringLiteral("Git Bash"), gitBashPath(),
                  {QStringLiteral("--login"), QStringLiteral("-i")}, environment);
    if (hasInstalledWslDistribution()) {
        appendProfile(profiles, QStringLiteral("wsl"), QStringLiteral("WSL"),
                      executablePath(QStringLiteral("wsl.exe")), {}, environment);
    }
#else
    const QString defaultShell = environment.value(QStringLiteral("SHELL"));
    if (!defaultShell.isEmpty()) {
        const QString shellName = QFileInfo(defaultShell).baseName();
        appendProfile(profiles, QStringLiteral("default-shell"),
                      shellName.isEmpty() ? QStringLiteral("Default Shell") : shellName,
                      defaultShell, {QStringLiteral("-i")}, environment);
    }
    appendProfile(profiles, QStringLiteral("bash"), QStringLiteral("Bash"),
                  executablePath(QStringLiteral("bash")), {QStringLiteral("-i")}, environment);
    appendProfile(profiles, QStringLiteral("zsh"), QStringLiteral("Zsh"),
                  executablePath(QStringLiteral("zsh")), {QStringLiteral("-i")}, environment);
    appendProfile(profiles, QStringLiteral("fish"), QStringLiteral("Fish"),
                  executablePath(QStringLiteral("fish")), {QStringLiteral("-i")}, environment);
    appendProfile(profiles, QStringLiteral("sh"), QStringLiteral("sh"),
                  executablePath(QStringLiteral("sh")), {QStringLiteral("-i")}, environment);
#endif
    if (profiles.isEmpty()) {
#ifdef Q_OS_WIN
        profiles.push_back({QStringLiteral("command-prompt"),
                            QStringLiteral("Command Prompt"),
                            QStringLiteral("cmd.exe"),
                            {QStringLiteral("/D"), QStringLiteral("/Q")},
                            environment});
#else
        profiles.push_back({QStringLiteral("default-shell"),
                            QStringLiteral("Shell"),
                            QStringLiteral("/bin/sh"),
                            {QStringLiteral("-i")},
                            environment});
#endif
    }
    return profiles;
}

TerminalProfile defaultTerminalProfile() { return availableTerminalProfiles().constFirst(); }

QProcessEnvironment refreshedProcessEnvironment() {
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
    QSettings userEnvironment(QStringLiteral("HKEY_CURRENT_USER\\Environment"),
                              QSettings::NativeFormat);
    const QString freshUserPath = userEnvironment.value(QStringLiteral("Path")).toString();
    const QString inheritedPath = environment.value(QStringLiteral("Path"));
    if (!freshUserPath.isEmpty()) {
        environment.insert(QStringLiteral("Path"), mergedWindowsPath(freshUserPath, inheritedPath));
    }
#endif
    return environment;
}

} // namespace litecode::terminal
