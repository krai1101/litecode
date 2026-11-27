#include "extensions/NativeExtensionInstaller.h"
#include "extensions/ExtensionManager.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLibrary>
#include <QLockFile>
#include <QPluginLoader>
#include <QTemporaryDir>

namespace litecode::extensions {
NativeExtensionInstallResult installNativeExtension(const QString& source,
                                                    const QString& extensionsDirectory) {
    NativeExtensionInstallResult result;
    const auto fail = [&result](const QString& error) {
        result.error = error;
        return result;
    };
    const QFileInfo selected(source);
    if (!selected.exists() || selected.isSymLink())
        return fail("Select a native extension library or package folder, not a link.");
    const QDir root(QFileInfo(extensionsDirectory).absoluteFilePath());
    if (!QDir().mkpath(root.absolutePath()))
        return fail("Cannot create the extensions folder.");
    QLockFile lock(root.filePath(".install.lock"));
    if (!lock.tryLock(0))
        return fail("Another extension installation is in progress.");
    const auto files =
        selected.isDir()
            ? QDir(selected.absoluteFilePath()).entryInfoList(QDir::Files | QDir::NoSymLinks)
            : QFileInfoList{selected};
    QFileInfo plugin;
    QJsonObject manifest;
    for (const auto& file : files) {
        if (!QLibrary::isLibrary(file.fileName()))
            continue;
        const auto metadata = QPluginLoader(file.absoluteFilePath()).metaData();
        if (!metadata.value("IID").toString().startsWith("org.litecode.Extension/"))
            continue;
        if (!plugin.filePath().isEmpty())
            return fail(
                "A package must contain exactly one LiteCode extension library at its root.");
        plugin = file;
        manifest = metadata.value("MetaData").toObject();
    }
    if (plugin.filePath().isEmpty()) {
        if (selected.isDir())
            return fail("No LiteCode extension library in this folder. Select the built package "
                        "folder containing the library and its bundled files.");
        if (selected.suffix().compare("vsix", Qt::CaseInsensitive) == 0)
            return fail("Select a native LiteCode package folder. VS Code .vsix packages are not "
                        "supported.");
        return fail("The selected file is not a native LiteCode extension library.");
    }
    const auto validation = ExtensionManager::validateManifest(manifest);
    if (!validation.isEmpty())
        return fail(validation);
    result.id = manifest.value("id").toString();
    // An existing version may still be loaded by a host. Never overwrite it or
    // silently install a duplicate identity; explicit update management is separate.
    QDirIterator installed(root.absolutePath(), QDir::Files | QDir::NoSymLinks,
                           QDirIterator::Subdirectories);
    while (installed.hasNext()) {
        const auto path = installed.next();
        if (QLibrary::isLibrary(path) &&
            QPluginLoader(path).metaData().value("MetaData").toObject().value("id").toString() ==
                result.id)
            return fail(
                "This extension is already installed. Existing libraries were not changed.");
    }
    QTemporaryDir stage(root.filePath(".install-XXXXXX"));
    if (!stage.isValid())
        return fail("Cannot stage the extension package.");
    const QDir staged(stage.path());
    if (selected.isDir()) {
        const QDir from(selected.absoluteFilePath());
        const auto relativeRoot = from.relativeFilePath(root.absolutePath());
        if (relativeRoot == "." ||
            (!relativeRoot.startsWith("../") && !QDir::isAbsolutePath(relativeRoot)))
            return fail("The source package must not contain the installed extensions folder.");
        QDirIterator contents(from.absolutePath(),
                              QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        qint64 total = 0;
        int count = 0;
        while (contents.hasNext()) {
            contents.next();
            const auto file = contents.fileInfo();
            if (file.isSymLink() || (!file.isDir() && !file.isFile()))
                return fail("Extension packages cannot contain links or special files.");
            if (++count > 20000 ||
                (total += file.isFile() ? file.size() : 0) > qint64(2) * 1024 * 1024 * 1024)
                return fail("Extension package exceeds the installation size limit.");
            const auto relative = from.relativeFilePath(file.absoluteFilePath());
            const auto target = staged.filePath(relative);
            if (file.isDir() ? !QDir().mkpath(target)
                             : (!QDir().mkpath(QFileInfo(target).absolutePath()) ||
                                !QFile::copy(file.absoluteFilePath(), target)))
                return fail("Cannot copy extension package: " + relative);
        }
    } else if (!QFile::copy(plugin.absoluteFilePath(), staged.filePath(plugin.fileName()))) {
        return fail("Cannot copy extension library.");
    }
    const auto stagedMetadata = QPluginLoader(staged.filePath(plugin.fileName())).metaData();
    if (stagedMetadata.value("MetaData").toObject() != manifest ||
        !stagedMetadata.value("IID").toString().startsWith("org.litecode.Extension/"))
        return fail("The extension changed while being copied. Installation was not committed.");
    const auto destination = root.filePath(result.id + '-' + manifest.value("version").toString());
    if (QFileInfo::exists(destination) || !QDir().rename(stage.path(), destination))
        return fail("Cannot commit extension installation. No installed library was replaced.");
    stage.setAutoRemove(false);
    result.path = QDir(destination).filePath(plugin.fileName());
    return result;
}
} // namespace litecode::extensions
