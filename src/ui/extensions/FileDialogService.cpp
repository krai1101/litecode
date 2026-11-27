#include "ui/extensions/FileDialogService.h"
#include "extensions/DialogWire.h"
#include "ui/components/ModalDialogQueue.h"
#include <QAbstractItemView>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QListView>
#include <QLineEdit>
#include <QJsonDocument>
#include <QTreeView>
#include <utility>

namespace litecode::ui {
namespace {
QString identity(const QString& owner, quint64 id) { return owner + QChar(0) + QString::number(id); }
QByteArray dialogKey(const extensions::FileDialogRequest& request) {
    QJsonObject options;
    if (const auto* open = std::get_if<extensions::OpenDialogOptions>(&request.options)) {
        options = extensions::wire::encodeOpenDialogOptions(*open);
        const bool folders = open->canSelectFolders.value_or(false);
        options.insert("canSelectFiles", open->canSelectFiles.value_or(false) || !folders);
        options.insert("canSelectFolders", folders);
        options.insert("canSelectMany", open->canSelectMany.value_or(false));
        options.insert("save", false);
    } else {
        options = extensions::wire::encodeSaveDialogOptions(std::get<extensions::SaveDialogOptions>(request.options));
        options.insert("save", true);
    }
    return QJsonDocument(options).toJson(QJsonDocument::Compact);
}
// Qt's native folder picker is single-selection on some platforms. This
// nonnative QFileDialog path retains the same workbench contract for multiple
// folders and macOS mixed file/folder selection.
class MultiResourceDialog final : public QFileDialog {
  public:
    MultiResourceDialog(QWidget* parent, bool foldersOnly, bool many) : QFileDialog(parent), foldersOnly_(foldersOnly), many_(many) {
        setOption(QFileDialog::DontUseNativeDialog);
        setFileMode(QFileDialog::ExistingFiles);
        for (auto* view : findChildren<QAbstractItemView*>())
            if (view->objectName() == "treeView" || view->objectName() == "listView")
                view->setSelectionMode(many_ ? QAbstractItemView::ExtendedSelection : QAbstractItemView::SingleSelection);
    }
    QStringList selection() const { return selection_; }
  protected:
    void accept() override {
        QStringList files;
        const auto* filename = findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (filename && filename->isModified()) files = selectedFiles();
        else {
            for (auto* view : findChildren<QAbstractItemView*>()) {
                if (!view->isVisible() || (view->objectName() != "treeView" && view->objectName() != "listView") || !view->selectionModel()) continue;
                for (const auto& index : view->selectionModel()->selectedRows()) {
                    const auto path = index.data(QFileSystemModel::FilePathRole).toString();
                    if (!path.isEmpty()) files.append(path);
                }
            }
        }
        if (files.isEmpty()) files = selectedFiles();
        files.removeDuplicates();
        if (files.isEmpty() || (!many_ && files.size() != 1)) return;
        for (const auto& file : files) {
            const QFileInfo info(file);
            if (!info.exists() || (foldersOnly_ && !info.isDir())) return;
        }
        selection_ = std::move(files);
        QDialog::accept();
    }
  private:
    bool foldersOnly_{};
    bool many_{};
    QStringList selection_;
};
QStringList nameFilters(const std::optional<QVector<std::pair<QString, QStringList>>>& filters) {
    QStringList result;
    if (!filters) return result;
    for (const auto& [label, extensions] : *filters) {
        QStringList patterns;
        for (const auto& extension : extensions) patterns.append(extension == "*" ? "*" : "*." + extension);
        if (patterns.isEmpty()) continue;
        auto name = label;
        for (const auto character : {QLatin1Char('('), QLatin1Char(')'), QLatin1Char(';'), QLatin1Char('\n'), QLatin1Char('\r')}) name.replace(character, QLatin1Char(' '));
        result.append(name + " (" + patterns.join(' ') + ')');
    }
    return result;
}
} // namespace
FileDialogService::FileDialogService(extensions::WorkbenchDialogs& dialogs, QWidget* owner,
    std::function<QUrl()> defaultUri, QObject* parent)
    : QObject(parent), dialogs_(&dialogs), owner_(owner), defaultUri_(std::move(defaultUri)) {
    if (owner_) modalQueue_ = &ModalDialogQueue::forWindow(*owner_);
    connect(&dialogs, &extensions::WorkbenchDialogs::requested, this, [this](extensions::FileDialogRequest request) {
        // Resolve defaults before fingerprinting, like upstream's workbench to
        // native conversion. The lock is per window, not per extension owner.
        std::visit([this](auto& options) {
            if (!options.defaultUri && defaultUri_) options.defaultUri = defaultUri_();
        }, request.options);
        if (auto* open = std::get_if<extensions::OpenDialogOptions>(&request.options))
            if (open->defaultUri && open->defaultUri->isLocalFile() && !QFileInfo::exists(open->defaultUri->toLocalFile()))
                open->defaultUri.reset();
        const auto key = dialogKey(request);
        if (dialogKeys_.values().contains(key)) {
            if (dialogs_) dialogs_->complete(request.owner, request.id, {});
            return;
        }
        dialogKeys_.insert(identity(request.owner, request.id), key);
        if (modalQueue_) modalQueue_->enqueue(*this, identity(request.owner, request.id), [this, request] { show(request); });
        else if (dialogs_) dialogs_->complete(request.owner, request.id, {}, tr("The workbench has stopped."));
    });
    connect(&dialogs, &extensions::WorkbenchDialogs::dismissed, this, [this](const QString& owner, quint64 id) { dismiss(owner, id); });
}
FileDialogService::~FileDialogService() {
    if (dialogs_) disconnect(dialogs_.data(), nullptr, this, nullptr);
    if (active_) { disconnect(active_.data(), nullptr, this, nullptr); delete active_.data(); }
}
void FileDialogService::show(const extensions::FileDialogRequest& request) {
    if (!dialogs_) { if (modalQueue_) modalQueue_->release(*this, identity(request.owner, request.id)); return; }
    const auto* open = std::get_if<extensions::OpenDialogOptions>(&request.options);
    const auto* save = std::get_if<extensions::SaveDialogOptions>(&request.options);
    auto initial = open ? open->defaultUri : save->defaultUri;
    if (!owner_ || (initial && !initial->isLocalFile())) {
        dialogs_->complete(request.owner, request.id, {}, !owner_ ? tr("The workbench has stopped.") : tr("File dialogs for this URI scheme require a filesystem provider."));
        return;
    }
    const bool folders = open && open->canSelectFolders.value_or(false);
    const bool files = open && (open->canSelectFiles.value_or(false) || !folders);
    const bool many = open && open->canSelectMany.value_or(false);
    bool mixed = false;
#ifdef Q_OS_MACOS
    mixed = folders && files;
#endif
    auto* dialog = (folders && many) || mixed ? static_cast<QFileDialog*>(new MultiResourceDialog(owner_, !mixed, many)) : new QFileDialog(owner_);
    dialog->setObjectName(QStringLiteral("extensionFileDialog"));
    dialog->setProperty("windowDialogOwner", request.owner);
    dialog->setProperty("windowDialogId", QString::number(request.id));
    dialog->setWindowModality(Qt::WindowModal);
    if (save) {
        dialog->setAcceptMode(QFileDialog::AcceptSave);
        dialog->setFileMode(QFileDialog::AnyFile);
        dialog->setWindowTitle(save->title && !save->title->isEmpty() ? *save->title : tr("Save File"));
        if (save->saveLabel && !save->saveLabel->isEmpty()) dialog->setLabelText(QFileDialog::Accept, *save->saveLabel);
    } else {
        dialog->setAcceptMode(QFileDialog::AcceptOpen);
        if (!dynamic_cast<MultiResourceDialog*>(dialog)) dialog->setFileMode(folders ? QFileDialog::Directory : many ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
        dialog->setWindowTitle(open->title && !open->title->isEmpty() ? *open->title : folders && !files ? tr("Open Folder") : tr("Open File"));
        if (open->openLabel && !open->openLabel->isEmpty()) dialog->setLabelText(QFileDialog::Accept, *open->openLabel);
    }
    const auto filters = nameFilters(open ? open->filters : save->filters);
    if (!filters.isEmpty()) dialog->setNameFilters(filters);
    if (initial) {
        const auto path = initial->toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) dialog->setDirectory(path);
        else { dialog->setDirectory(info.absolutePath()); dialog->selectFile(info.fileName()); }
    }
    active_ = dialog;
    activeOwner_ = request.owner;
    activeId_ = request.id;
    connect(dialog, &QDialog::accepted, this, [this, request, dialog] {
        if (!dialogs_) return;
        const auto* multi = dynamic_cast<MultiResourceDialog*>(dialog);
        const auto paths = multi ? multi->selection() : dialog->selectedFiles();
        QVector<QUrl> resources;
        for (const auto& path : paths) {
            auto selected = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_MACOS
            selected = selected.normalized(QString::NormalizationForm_C);
#endif
            resources.append(QUrl::fromLocalFile(selected));
        }
        if (!dialogs_->complete(request.owner, request.id, resources.isEmpty() ? std::nullopt : std::optional<QVector<QUrl>>(std::move(resources))))
            dialogs_->complete(request.owner, request.id, {}, tr("The file dialog returned an invalid selection."));
    });
    connect(dialog, &QDialog::rejected, this, [this, request] { if (dialogs_) dialogs_->complete(request.owner, request.id, {}); });
    dialog->open();
}
void FileDialogService::dismiss(const QString& owner, quint64 id) {
    dialogKeys_.remove(identity(owner, id));
    if (active_ && activeOwner_ == owner && activeId_ == id) {
        auto* dialog = active_.data();
        active_.clear();
        activeOwner_.clear();
        activeId_ = 0;
        dialog->hide();
        dialog->deleteLater();
    }
    if (modalQueue_) modalQueue_->release(*this, identity(owner, id));
}
} // namespace litecode::ui
