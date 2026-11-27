#include "ui/ExtensionsPanel.h"
#include "ui/extensions/ExtensionDetailsPage.h"
#include "extensions/ExtensionManager.h"
#include "extensions/ConfigurationService.h"
#include "extensions/NativeExtensionInstaller.h"
#include "ui/commands/CommandRegistry.h"
#include "ui/ConfirmationDialog.h"
#include "ui/Theme.h"
#include "ui/ThemedIcon.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVBoxLayout>

namespace litecode::ui {
namespace {
class ExtensionDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {0, 68};
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const bool dark = qApp->property("litecodeDarkTheme").toBool();
        const auto theme = Theme::tokens(dark);
        painter->save();
        painter->setClipRect(option.rect);
        const bool selected = option.state & QStyle::State_Selected;
        painter->fillRect(option.rect, selected ? theme.listSelectionBackground
                                       : option.state & QStyle::State_MouseOver
                                           ? theme.listHoverBackground
                                           : theme.sidebarSurface);
        auto icon = QIcon(index.data(Qt::UserRole + 3).toString());
        if (icon.isNull()) icon = themedIcon(":/icons/extensions.svg", dark);
        icon.paint(painter, QRect(option.rect.left() + 12, option.rect.top() + 14, 32, 32));
        auto font = option.font;
        font.setPixelSize(13);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(selected ? theme.listSelectionForeground : theme.foreground);
        const QRect nameRect(option.rect.left() + 54, option.rect.top() + 8,
                             qMax(0, option.rect.width() - 66), 19);
        painter->drawText(nameRect, Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight,
                                                        nameRect.width()));
        font.setBold(false);
        font.setPixelSize(12);
        painter->setFont(font);
        painter->setPen(theme.mutedForeground);
        const QRect infoRect(nameRect.left(), option.rect.top() + 28, nameRect.width(), 18);
        painter->drawText(infoRect, Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(index.data(Qt::UserRole).toString(),
                                                        Qt::ElideRight, infoRect.width()));
        const QRect stateRect(nameRect.left(), option.rect.top() + 46, nameRect.width(), 16);
        painter->drawText(stateRect, Qt::AlignVCenter,
                          QFontMetrics(font).elidedText(index.data(Qt::UserRole + 2).toString(),
                                                        Qt::ElideRight, stateRect.width()));
        painter->restore();
    }
};
} // namespace
ExtensionsPanel::ExtensionsPanel(CommandRegistry* commands, QWidget* parent) : QWidget(parent) {
    setObjectName("extensionsPanel");
    setAttribute(Qt::WA_StyledBackground, true);
    directory_ =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
            .filePath("extensions");
    manager_ = new extensions::ExtensionManager(directory_, this);
    connect(&manager_->configuration(), &extensions::ConfigurationService::fileUpdated,
            this, &ExtensionsPanel::configurationFileUpdated);
    connect(&manager_->configuration(), &extensions::ConfigurationService::diagnostic,
            this, &ExtensionsPanel::configurationDiagnostic);
    QPointer<CommandRegistry> registry(commands);
    manager_->setWorkbenchCommandSource(
        [registry] {
            QStringList ids;
            if (!registry) return ids;
            for (const auto& id : registry->registeredIds())
                if (!id.startsWith("extension:")) ids.append(id);
            return ids;
        },
        [registry](const QString& id) { return registry && registry->invoke(id); });
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName("extensionsToolbar");
    toolbar->setFixedHeight(28);
    auto* tools = new QHBoxLayout(toolbar);
    tools->setContentsMargins(12, 0, 8, 0);
    auto* heading = new QLabel(tr("LOCAL"), toolbar);
    tools->addWidget(heading, 1);
    auto* more = new components::IconButton(toolbar);
    more->setIconSize({16, 16});
    more->setToolTip(tr("Extension actions"));
    setThemedIcon(more, ":/icons/more.svg");
    tools->addWidget(more);
    layout->addWidget(toolbar);
    auto* searchArea = new QWidget(this);
    searchArea->setFixedHeight(44);
    auto* searchLayout = new QVBoxLayout(searchArea);
    searchLayout->setContentsMargins(12, 4, 12, 8);
    filter_ = new components::Input(searchArea);
    filter_->setObjectName("extensionSearch");
    filter_->setPlaceholderText(tr("Search installed extensions"));
    searchLayout->addWidget(filter_);
    layout->addWidget(searchArea);
    importStatus_ = new QLabel(this);
    importStatus_->setTextFormat(Qt::PlainText);
    importStatus_->setWordWrap(true);
    importStatus_->setContentsMargins(12, 4, 12, 4);
    importStatus_->hide();
    layout->addWidget(importStatus_);
    installed_ = new QLabel(this);
    installed_->setObjectName("extensionsInstalled");
    installed_->setFixedHeight(28);
    layout->addWidget(installed_);
    list_ = new components::List(this);
    list_->setObjectName("nativeExtensions");
    list_->setMouseTracking(true);
    list_->setTextElideMode(Qt::ElideRight);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setItemDelegate(new ExtensionDelegate(list_));
    TransientScrollBars::install(list_);
    layout->addWidget(list_, 1);
    empty_ = new QLabel(this);
    empty_->setObjectName("extensionsEmpty");
    empty_->setTextFormat(Qt::PlainText);
    empty_->setWordWrap(true);
    empty_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    empty_->setContentsMargins(12, 8, 12, 12);
    layout->addWidget(empty_);
    empty_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    layout->insertWidget(layout->indexOf(list_), empty_);
    connect(filter_, &QLineEdit::textChanged, this, &ExtensionsPanel::render);
    connect(more, &QToolButton::clicked, this, [this, more] {
        components::Menu menu(this);
        menu.addAction(tr("Install Extension from File…"), this, [this] { chooseImport(false); })->setEnabled(!importing_);
        menu.addAction(tr("Install Extension from Folder…"), this, [this] { chooseImport(true); })->setEnabled(!importing_);
        menu.exec(more->mapToGlobal(QPoint(0, more->height())));
    });
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* item = list_->itemAt(point);
        if (!item)
            return;
        list_->setCurrentItem(item);
        const auto id = item->data(Qt::UserRole).toString();
        const bool enabled = item->data(Qt::UserRole + 1).toBool();
        components::Menu menu(this);
        if (enabled) {
            for (const auto& view : manager_->nativeViews().contributions()) {
                if (view.owner != id) continue;
                menu.addAction(tr("Open %1").arg(view.title), this,
                    [this, viewId = view.id] { emit nativeViewOpenRequested(viewId); });
            }
        }
        menu.addAction(enabled ? tr("Disable") : tr("Enable"), this,
                       [this, id, enabled] { changeEnabled(id, !enabled); })->setEnabled(!importing_);
        menu.addAction(tr("Uninstall"), this,
                       [this, id] { uninstallExtension(id); })->setEnabled(!importing_);
        menu.addSeparator();
        menu.addAction(tr("Show Details"), this, [this, id] {
            emit detailsRequested(id);
        });
        menu.exec(list_->viewport()->mapToGlobal(point));
    });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit detailsRequested(item->data(Qt::UserRole).toString());
    });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit detailsRequested(item->data(Qt::UserRole).toString());
    });
    connect(manager_, &extensions::ExtensionManager::changed, this, &ExtensionsPanel::render);
    connect(manager_, &extensions::ExtensionManager::uninstallFinished, this,
            [this](const QString& id, bool removed, const QString& error) {
        importing_ = false;
        importStatus_->setText(error.isEmpty() ? (removed ? tr("Uninstalled %1.").arg(id)
                                                       : tr("Could not uninstall %1.").arg(id)) : error);
        importStatus_->show();
        emit enabledStateChanged();
    });
    connect(manager_, &extensions::ExtensionManager::openFileRequested, this,
            &ExtensionsPanel::openFileRequested);
    connect(manager_, &extensions::ExtensionManager::informationMessage, this,
            [this](const QString& message) {
                ConfirmationDialog::showMessage(this, tr("Extension"), message);
            });
    connect(manager_, &extensions::ExtensionManager::documentFormatResult, this,
            &ExtensionsPanel::documentFormatResult);
    connect(manager_, &extensions::ExtensionManager::debugAdapterResult, this,
            &ExtensionsPanel::debugAdapterResult);
    render();
}
void ExtensionsPanel::chooseImport(bool folder) {
    if (importing_) return;
    auto* picker = new QFileDialog(this, folder ? tr("Install Native Extension from Folder") : tr("Install Native Extension from File"));
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setFileMode(folder ? QFileDialog::Directory : QFileDialog::ExistingFile);
    if (folder) {
        picker->setOption(QFileDialog::ShowDirsOnly);
        const QDir executableDirectory(QCoreApplication::applicationDirPath());
        for (const auto* relative : {"../../extension-packages", "../extensions"}) {
            const QDir packages(executableDirectory.filePath(relative));
            if (packages.exists()) {
                picker->setDirectory(packages.absolutePath());
                break;
            }
        }
    } else {
        picker->setNameFilter(tr("Native extensions (*.dll *.so *.dylib);;All files (*)"));
    }
    connect(picker, &QFileDialog::fileSelected, this, &ExtensionsPanel::importExtension);
    picker->open();
}
void ExtensionsPanel::importExtension(const QString& source) {
    if (importing_ || source.isEmpty()) return;
    importing_ = true;
    importStatus_->setText(tr("Installing native extension…"));
    importStatus_->show();
    auto* watcher = new QFutureWatcher<extensions::NativeExtensionInstallResult>(this);
    connect(watcher, &QFutureWatcher<extensions::NativeExtensionInstallResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        importing_ = false;
        if (!result.error.isEmpty()) {
            importStatus_->setText(result.error);
            return;
        }
        manager_->discover();
        revealExtension(result.id);
        importStatus_->setText(tr("Installed %1. Right-click it and choose Enable.").arg(result.id));
    });
    const auto directory = directory_;
    watcher->setFuture(QtConcurrent::run([source, directory] {
        return extensions::installNativeExtension(source, directory);
    }));
}
void ExtensionsPanel::discover() { manager_->discover(); }
void ExtensionsPanel::revealExtension(const QString& id) {
    filter_->clear();
    for (int row = 0; row < list_->count(); ++row) {
        auto* item = list_->item(row);
        if (item->data(Qt::UserRole).toString() != id) continue;
        list_->setCurrentItem(item);
        list_->scrollToItem(item);
        list_->setFocus(Qt::OtherFocusReason);
        break;
    }
}
ExtensionDetailsPage* ExtensionsPanel::createDetailsPage(const QString& id) {
    auto* page = new ExtensionDetailsPage(*manager_, id);
    connect(page, &ExtensionDetailsPage::enableRequested, this,
            [this, id](bool enabled) { changeEnabled(id, enabled); });
    connect(page, &ExtensionDetailsPage::uninstallRequested, this,
            [this, id] { uninstallExtension(id); });
    return page;
}
void ExtensionsPanel::changeEnabled(const QString& id, bool enabled) {
    if (importing_) return;
    if (!manager_->setEnabled(id, enabled))
        ConfirmationDialog::showMessage(this, tr("Extensions"),
                                        tr("Could not change extension state. Check its diagnostic."));
    else emit enabledStateChanged();
}
void ExtensionsPanel::uninstallExtension(const QString& id) {
    if (importing_) return;
    if (ConfirmationDialog::ask(this, tr("Uninstall Extension"),
            tr("Uninstall %1? Its host will stop and its installed package will be removed. "
               "Settings and saved chat history will be kept.").arg(id),
            tr("Uninstall"), {}, tr("Cancel")) != ConfirmationChoice::Primary) return;
    importing_ = true;
    importStatus_->setText(tr("Uninstalling %1?").arg(id));
    importStatus_->show();
    if (!manager_->uninstall(id)) {
        importing_ = false;
        importStatus_->setText(tr("Could not start uninstall. Another operation may be in progress."));
    }
}
void ExtensionsPanel::setWorkspace(const QString& path) { manager_->setWorkspace(path); }
void ExtensionsPanel::activateForLanguage(const QString& languageId) {
    manager_->activateForLanguage(languageId);
}
void ExtensionsPanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        refreshThemedIcons(this, qApp->property("litecodeDarkTheme").toBool());
}
QStringList ExtensionsPanel::commandIds() const {
    QStringList ids;
    for (const auto& contribution : manager_->commandContributions())
        ids.append(contribution.id);
    return ids;
}
QVector<extensions::DebuggerContribution> ExtensionsPanel::debuggerContributions() const {
    return manager_->debuggerContributions();
}
quint64 ExtensionsPanel::requestDebugAdapter(const QString& type,
                                            const QJsonObject& configuration, const QString& sessionId, const QJsonValue& folderOverride) {
    return manager_->requestDebugAdapter(type, configuration, sessionId, folderOverride);
}
QString ExtensionsPanel::commandTitle(const QString& id) const {
    for (const auto& command : manager_->commandContributions())
        if (command.id == id)
            return command.category.isEmpty() ? command.title
                                              : command.category + ": " + command.title;
    return id;
}
void ExtensionsPanel::executeCommand(const QString& id) {
    if (!manager_->executeCommand(id))
        ConfirmationDialog::showMessage(this, tr("Extensions"), tr("Extension command failed."));
}
QVector<extensions::FormatterContribution>
ExtensionsPanel::formatterContributions(const QString& languageId) const {
    return manager_->formatterContributions(languageId);
}
quint64 ExtensionsPanel::requestDocumentFormat(const QString& languageId, const QString& text,
                                               const QString& extensionId) {
    return manager_->requestDocumentFormat(languageId, text, extensionId);
}
void ExtensionsPanel::setConfigurationWriteGuard(std::function<QString(const QString&)> guard) {
    manager_->configuration().setWriteGuard(std::move(guard));
}
void ExtensionsPanel::render() {
    const QString selected =
        list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toString() : QString();
    list_->clear();
    const auto extensions = manager_->extensions();
    installed_->setText(tr("INSTALLED  %1").arg(extensions.size()));
    const auto query = filter_->text().trimmed();
    for (const auto& extension : extensions) {
        if (!query.isEmpty() &&
            !(extension.name + " " + extension.id).contains(query, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(extension.name + "  " + extension.version, list_);
        item->setData(Qt::UserRole, extension.id);
        item->setData(Qt::UserRole + 1, extension.enabled);
        item->setData(Qt::UserRole + 3, extension.iconPath);
        item->setData(Qt::UserRole + 2, extension.uninstalling ? tr("Uninstalling…")
                                      : !extension.error.isEmpty() ? extension.error
                                        : extension.enabled        ? tr("Enabled")
                                                                   : tr("Disabled"));
        item->setToolTip(extension.path + "\n" + extension.error);
        if (extension.id == selected)
            list_->setCurrentItem(item);
    }
    empty_->setText(extensions.isEmpty()
                        ? tr("No extensions installed. Use the actions menu to install a native "
                             "extension from a file or folder, then enable it.")
                        : tr("No matching extensions."));
    empty_->setVisible(list_->count() == 0);
    list_->setVisible(true);
    emit commandsChanged(commandIds());
    emit debuggersChanged();
}
} // namespace litecode::ui
