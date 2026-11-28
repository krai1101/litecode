#pragma once
#include <QString>

namespace litecode::extensions {
struct NativeExtensionInstallResult {
    QString id, path, error;
};
// Private package management, not part of the extension-facing SDK. Inspects Qt
// metadata without loading code. Install never grants trust or replaces a DLL.
NativeExtensionInstallResult installNativeExtension(const QString& source,
                                                    const QString& extensionsDirectory);
} // namespace litecode::extensions
