#include "WindowsJumpList.h"
#include "core/SettingsService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <array>
#include <windows.h>
#include <shlobj.h>
#include <propkey.h>
#include <propvarutil.h>
#include <wrl/client.h>

namespace litecode::app {
namespace {
using Microsoft::WRL::ComPtr;

QString argumentsForPath(const QString& path, bool folder) {
    QString native = QDir::toNativeSeparators(path);
    // Windows paths cannot contain quotes. Double trailing backslashes so a
    // drive root's final separator does not escape the closing quote.
    qsizetype end = native.size();
    while (end > 0 && native.at(end - 1) == '\\')
        --end;
    native.append(QString(native.size() - end, QChar('\\')));
    return (folder ? QStringLiteral("--new-window --folder \"")
                   : QStringLiteral("--new-window --file \"")) + native + '"';
}

ComPtr<IShellLinkW> shellLink(const QString& target, const QString& arguments,
                             const QString& title, const QString& description = {},
                             const QString& icon = {}, int iconIndex = 0) {
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(link.GetAddressOf()))) ||
        FAILED(link->SetPath(target.toStdWString().c_str())) ||
        FAILED(link->SetArguments(arguments.toStdWString().c_str())) ||
        FAILED(link->SetIconLocation((icon.isEmpty() ? target : icon).toStdWString().c_str(),
                                     iconIndex)) ||
        FAILED(link->SetDescription(description.left(255).toStdWString().c_str())))
        return {};
    ComPtr<IPropertyStore> properties;
    if (FAILED(link.As(&properties)))
        return {};
    PROPVARIANT value{};
    if (FAILED(InitPropVariantFromString(title.left(255).toStdWString().c_str(), &value)))
        return {};
    const HRESULT result = properties->SetValue(PKEY_Title, value);
    PropVariantClear(&value);
    if (FAILED(result) || FAILED(properties->Commit()))
        return {};
    return link;
}

ComPtr<IObjectCollection> collection() {
    ComPtr<IObjectCollection> objects;
    CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(objects.GetAddressOf()));
    return objects;
}

QSet<QString> removedArguments(IObjectArray& removed, const QString& executable) {
    QSet<QString> result;
    UINT count = 0;
    if (FAILED(removed.GetCount(&count)))
        return result;
    std::array<wchar_t, 32768> buffer{};
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IShellLinkW> link;
        if (SUCCEEDED(removed.GetAt(index, IID_PPV_ARGS(link.GetAddressOf()))) &&
            SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr,
                                    SLGP_RAWPATH)) &&
            QString::fromWCharArray(buffer.data()).compare(executable, Qt::CaseInsensitive) == 0 &&
            SUCCEEDED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size()))))
            result.insert(QString::fromWCharArray(buffer.data()));
    }
    return result;
}

HRESULT appendCategory(ICustomDestinationList& list, const QString& executable,
                       const QStringList& paths, bool folders, qsizetype limit) {
    if (paths.isEmpty() || limit <= 0)
        return S_OK;
    auto items = collection();
    if (!items)
        return E_FAIL;
    SHSTOCKICONINFO icon{sizeof(SHSTOCKICONINFO)};
    if (folders)
        SHGetStockIconInfo(SIID_FOLDER, SHGSI_ICONLOCATION, &icon);
    UINT added = 0;
    for (const auto& path : paths) {
        const QString name = QFileInfo(path).fileName();
        const QString nativePath = QDir::toNativeSeparators(path);
        auto link = shellLink(executable, argumentsForPath(path, folders),
                              name.isEmpty() ? nativePath : name, nativePath,
                              QString::fromWCharArray(icon.szPath), icon.iIcon);
        if (link && SUCCEEDED(items->AddObject(link.Get())) && ++added >= limit)
            break;
    }
    return added ? list.AppendCategory(folders ? L"Recent Folders" : L"Recent Files", items.Get())
                 : S_OK;
}

void update(core::SettingsService& settings) {
    ComPtr<ICustomDestinationList> list;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(list.GetAddressOf()))))
        return;
    // Use Windows' existing executable identity, including for portable copies.
    UINT availableSlots = 0;
    ComPtr<IObjectArray> removed;
    if (FAILED(list->BeginList(&availableSlots, IID_PPV_ARGS(removed.GetAddressOf()))))
        return;
    const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    auto folders = settings.recentWorkspaceFolders();
    auto files = settings.recentFiles();
    if (removed) {
        const auto suppressed = removedArguments(*removed.Get(), executable);
        const auto filterRemoved = [&settings, &suppressed](const QString& path, bool folder) {
            if (!suppressed.contains(argumentsForPath(path, folder)))
                return false;
            settings.forgetRecentPath(path);
            return true;
        };
        folders.removeIf([&](const QString& path) { return filterRemoved(path, true); });
        files.removeIf([&](const QString& path) { return filterRemoved(path, false); });
    }
    const auto tasks = collection();
    const auto newWindow = shellLink(executable, QStringLiteral("--new-window"),
                                     QStringLiteral("New Window"));
    if (!tasks || !newWindow || FAILED(tasks->AddObject(newWindow.Get())) ||
        FAILED(list->AddUserTasks(tasks.Get()))) {
        list->AbortList();
        return;
    }
    const qsizetype fileReserve = qMin(qsizetype{3}, files.size());
    const qsizetype destinationLimit = availableSlots;
    const qsizetype folderCount =
        qMin(folders.size(), qMax(qsizetype{0}, destinationLimit - fileReserve));
    const HRESULT folderResult = appendCategory(*list.Get(), executable, folders, true, folderCount);
    const HRESULT fileResult = appendCategory(*list.Get(), executable, files, false,
                                               destinationLimit - folderCount);
    // The OS can disable recent items while still allowing user tasks.
    if ((FAILED(folderResult) && folderResult != E_ACCESSDENIED) ||
        (FAILED(fileResult) && fileResult != E_ACCESSDENIED) || FAILED(list->CommitList()))
        list->AbortList();
}
} // namespace

void publishWindowsJumpList(core::SettingsService& settings) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return;
    update(settings);
    CoUninitialize();
}
} // namespace litecode::app
