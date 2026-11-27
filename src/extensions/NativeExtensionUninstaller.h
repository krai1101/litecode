#pragma once
#include <QString>
namespace litecode::extensions {
struct NativeExtensionRemovalResult {
    bool removed{};
    QString error;
};
// Private package management. Never removes extension state or settings.
NativeExtensionRemovalResult removeNativeExtension(const QString& library,
                                                   const QString& extensionsDirectory,
                                                   const QString& id, const QString& version);
} // namespace litecode::extensions
