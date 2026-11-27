#include "ui/extensions/ExtensionDetailsPage.h"
#include "ui/extensions/NativeMarkdownView.h"
#include "extensions/ExtensionManager.h"
#include "ui/ThemedIcon.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>
namespace litecode::ui {
namespace {
QString packageReadme(const QString& libraryPath) {
    constexpr qint64 readmeLimit = 256 * 1024;
    const QDir package = QFileInfo(libraryPath).absoluteDir();
    for (const auto& name : {QStringLiteral("README.md"), QStringLiteral("readme.md")}) {
        const QFileInfo info(package.filePath(name));
        if (!info.isFile() || info.isSymLink() || info.size() > readmeLimit)
            continue;
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto text = file.read(readmeLimit + 1);
        if (text.size() <= readmeLimit)
            return QString::fromUtf8(text).trimmed();
    }
    return {};
}
} // namespace
ExtensionDetailsPage::ExtensionDetailsPage(extensions::ExtensionManager& manager, QString id,
                                           QWidget* parent)
    : QWidget(parent), manager_(&manager), id_(std::move(id)) {
    setObjectName("extensionDetailsPage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("extensionDetailsScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget(scroll);
    body->setObjectName("extensionDetailsBody");
    auto* content = new QVBoxLayout(body);
    content->setContentsMargins(32, 28, 32, 28);
    content->setSpacing(16);
    auto* header = new QHBoxLayout;
    header->setSpacing(24);
    icon_ = new QLabel(body);
    icon_->setFixedSize(80, 80);
    header->addWidget(icon_, 0, Qt::AlignTop);
    auto* summary = new QVBoxLayout;
    name_ = new QLabel(body);
    name_->setObjectName("extensionDetailsName");
    auto titleFont = name_->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    name_->setFont(titleFont);
    identity_ = new QLabel(body);
    summary->addWidget(name_);
    summary->addWidget(identity_);
    description_ = new QLabel(body);
    summary->addWidget(description_);
    auto* actions = new QHBoxLayout;
    enable_ = new components::Button({}, body, components::ButtonVariant::Primary);
    enable_->setObjectName("extensionDetailsEnable");
    uninstall_ = new components::Button(tr("Uninstall"), body);
    uninstall_->setObjectName("extensionDetailsUninstall");
    actions->addWidget(enable_);
    actions->addWidget(uninstall_);
    actions->addStretch();
    summary->addLayout(actions);
    header->addLayout(summary, 1);
    content->addLayout(header);
    auto* installation = new QWidget(body);
    installation->setObjectName("extensionDetailsInstallation");
    auto* installationLayout = new QVBoxLayout(installation);
    installationLayout->setContentsMargins(0, 0, 0, 0);
    installationLayout->setSpacing(6);
    auto* installationHeading = new QLabel(tr("Installation"), installation);
    auto headingFont = installationHeading->font();
    headingFont.setBold(true);
    installationHeading->setFont(headingFont);
    status_ = new QLabel(installation);
    status_->setObjectName("extensionDetailsStatus");
    diagnostic_ = new QLabel(installation);
    diagnostic_->setObjectName("extensionDetailsDiagnostic");
    location_ = new QLabel(installation);
    location_->setObjectName("extensionDetailsLocation");
    installationLayout->addWidget(installationHeading);
    installationLayout->addWidget(status_);
    installationLayout->addWidget(diagnostic_);
    installationLayout->addWidget(location_);
    content->addWidget(installation);

    documentation_ = new QWidget(body);
    documentation_->setObjectName("extensionDetailsDocumentation");
    auto* documentationLayout = new QVBoxLayout(documentation_);
    documentationLayout->setContentsMargins(0, 0, 0, 0);
    documentationLayout->setSpacing(12);
    auto* heading = new QLabel(tr("Documentation"), documentation_);
    heading->setFont(headingFont);
    documentationLayout->addWidget(heading);
    readme_ = new NativeMarkdownView(true, documentation_, false);
    readme_->setObjectName("extensionDetailsReadme");
    readme_->setAccessibleName(tr("Extension documentation"));
    readme_->viewport()->setCursor(Qt::IBeamCursor);
    connect(readme_, &QTextBrowser::highlighted, readme_, [readme = readme_](const QUrl& url) {
        if (url.isEmpty())
            readme->viewport()->setCursor(Qt::IBeamCursor);
    });
    documentation_->hide();
    connect(readme_, &QTextBrowser::anchorClicked, this, [](const QUrl& url) {
        if ((url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty())
            QDesktopServices::openUrl(url);
    });
    documentationLayout->addWidget(readme_);
    content->addWidget(documentation_);
    for (auto* label :
         {name_, identity_, description_, installationHeading, heading, status_, diagnostic_, location_}) {
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setCursor(Qt::IBeamCursor);
    }
    content->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll);
    TransientScrollBars::install(scroll);
    connect(enable_, &QPushButton::clicked, this, [this] { emit enableRequested(!enabled_); });
    connect(uninstall_, &QPushButton::clicked, this, &ExtensionDetailsPage::uninstallRequested);
    connect(&manager, &extensions::ExtensionManager::changed, this, &ExtensionDetailsPage::refresh);
    connect(&manager, &QObject::destroyed, this, &ExtensionDetailsPage::refresh);
    refresh();
}
void ExtensionDetailsPage::setCodeFont(const QFont& font) { readme_->setCodeFont(font); }

void ExtensionDetailsPage::refresh() {
    if (manager_)
        for (const auto& extension : manager_->extensions()) {
            if (extension.id != id_)
                continue;
            enabled_ = extension.enabled;
            setWindowTitle(extension.name);
            name_->setText(extension.name);
            identity_->setText(extension.id + "  ·  " + extension.version);
            description_->setText(extension.description);
            description_->setVisible(!extension.description.trimmed().isEmpty());
            if (documentedPackage_ != extension.path) {
                documentedPackage_ = extension.path;
                const auto readme = packageReadme(extension.path);
                readme_->setMarkdownText(readme);
                documentation_->setVisible(!readme.isEmpty());
            }
            icon_->setPixmap((extension.iconPath.isEmpty()
                     ? themedIcon(":/icons/extensions.svg",
                                               qApp->property("litecodeDarkTheme").toBool())
                                  : QIcon(extension.iconPath))
                                 .pixmap(icon_->size(), icon_->devicePixelRatioF()));
            status_->setText(extension.uninstalling ? tr("Uninstalling…")
                             : extension.enabled
                                 ? (extension.active ? tr("Enabled · Running") : tr("Enabled"))
                                 : tr("Disabled"));
            diagnostic_->setText(extension.error);
            diagnostic_->setVisible(!extension.error.isEmpty());
            location_->setText(
                tr("Location\n%1")
                    .arg(QDir::toNativeSeparators(QFileInfo(extension.path).absolutePath())));
            location_->show();
            enable_->setText(extension.enabled ? tr("Disable") : tr("Enable"));
            enable_->setEnabled(!extension.uninstalling);
            uninstall_->setEnabled(!extension.uninstalling);
            return;
        }
    setWindowTitle(id_);
    name_->setText(id_);
    identity_->clear();
    description_->hide();
    documentedPackage_.clear();
    documentation_->hide();
    status_->setText(tr("This extension is no longer installed."));
    diagnostic_->hide();
    location_->clear();
    location_->hide();
    enable_->setEnabled(false);
    uninstall_->setEnabled(false);
}
void ExtensionDetailsPage::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (readme_ && enable_ && (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange))
        refresh();
}

} // namespace litecode::ui
