#include "extensions/NativeExtensionUninstaller.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLibrary>
#include <QLockFile>
#include <QPluginLoader>
#include <QTemporaryDir>
namespace litecode::extensions {
NativeExtensionRemovalResult removeNativeExtension(const QString& library,
                                                   const QString& extensionsDirectory,
                                                   const QString& id, const QString& version) {
    const auto fail = [](const QString& error) {
        return NativeExtensionRemovalResult{false, error};
    };
    const auto canonicalRoot = QFileInfo(extensionsDirectory).canonicalFilePath();
    if (canonicalRoot.isEmpty())
        return fail("The extensions folder does not exist.");
    const QDir root(canonicalRoot);
    const QFileInfo file(library);
    if (!file.isFile() || file.isSymLink() || file.canonicalFilePath().isEmpty())
        return fail("The installed extension path is missing or linked. No files were removed.");
    const auto metadata = QPluginLoader(library).metaData();
    const auto manifest = metadata.value("MetaData").toObject();
    if (!QLibrary::isLibrary(library) ||
        !metadata.value("IID").toString().startsWith("org.litecode.Extension/") ||
        manifest.value("id").toString() != id || manifest.value("version").toString() != version)
        return fail("The installed extension identity changed. No files were removed.");
    const auto parent = file.dir().canonicalPath();
    const bool loose = parent == root.path();
    const auto package = root.filePath(id + '-' + version);
    if (!loose && (parent != package || QFileInfo(package).isSymLink() ||
                   QFileInfo(package).canonicalFilePath() != package))
        return fail("The extension is outside its managed package folder. No files were removed.");
    QLockFile lock(root.filePath(".install.lock"));
    if (!lock.tryLock(0))
        return fail("Another extension package operation is in progress. Try again.");
    if (!loose) {
        for (const auto& sibling : QDir(package).entryInfoList(QDir::Files | QDir::NoSymLinks)) {
            if (!QLibrary::isLibrary(sibling.fileName()))
                continue;
            const auto other = QPluginLoader(sibling.absoluteFilePath()).metaData();
            if (other.value("IID").toString().startsWith("org.litecode.Extension/") &&
                other.value("MetaData").toObject().value("id").toString() != id)
                return fail("The folder contains another extension. No files were removed.");
        }
        QDirIterator contents(package,
                              QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (contents.hasNext()) {
            contents.next();
            const auto item = contents.fileInfo();
            if (item.isSymLink() || (!item.isDir() && !item.isFile()))
                return fail("The package contains links or special files. No files were removed.");
        }
    }
    // Move into a nested quarantine before deleting, so partial deletion cannot
    // leave a discoverable plugin. Settings/storage are siblings, never targets.
    QTemporaryDir retired(root.filePath(".uninstall-XXXXXX"));
    if (!retired.isValid() || !QDir().mkpath(retired.filePath("payload")))
        return fail("Cannot prepare extension removal. No files were removed.");
    const auto source = loose ? file.canonicalFilePath() : package;
    const auto destination = retired.filePath("payload/" + QFileInfo(source).fileName());
    if (!QDir().rename(source, destination))
        return fail("Cannot remove the extension package. Check file permissions and try again.");
    if (!retired.remove()) {
        retired.setAutoRemove(false);
        return {true,
                "Extension uninstalled, but some files could not be deleted: " + retired.path()};
    }
    return {true, {}};
}
} // namespace litecode::extensions
