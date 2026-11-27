#include "ui/chat/ParticipantResponseView.h"
#include "extensions/ExtensionManager.h"
#include "ui/components/Controls.h"
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace litecode::ui::chat {
namespace {
class ResponseMarkdown final : public QTextBrowser {
  public:
    explicit ResponseMarkdown(QWidget* parent) : QTextBrowser(parent) {
        setFrameShape(QFrame::NoFrame);
        setOpenLinks(false);
        setOpenExternalLinks(false);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(document(), &QTextDocument::documentLayoutChanged, this, [this] { fit(); });
    }
    void setResponseMarkdown(const QString& text) {
        // QTextDocument must not load extension-provided local/network images.
        // HTML stays disabled until a reviewed sanitizer is available.
        document()->setMarkdown(text, QTextDocument::MarkdownNoHTML);
        fit();
    }

  protected:
    QVariant loadResource(int, const QUrl&) override { return {}; }
    void resizeEvent(QResizeEvent* event) override {
        QTextBrowser::resizeEvent(event);
        fit();
    }

  private:
    void fit() {
        document()->setTextWidth(qMax(1, viewport()->width()));
        setFixedHeight(qMax(28, static_cast<int>(document()->size().height()) + 8));
    }
};
QLabel* plainLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
QUrl resourceUri(const QJsonValue& value) {
    return QUrl(value.isString() ? value.toString() : value.toObject().value("uri").toString());
}
void addNodes(const QJsonArray& nodes, QTreeWidget* tree, QTreeWidgetItem* parent,
              const QUrl& base) {
    for (const auto& value : nodes) {
        const auto node = value.toObject();
        const auto name = node.value("name").toString();
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setText(0, name);
        auto uri = base;
        uri.setPath(base.path() + (base.path().endsWith('/') ? "" : "/") + name);
        item->setData(0, Qt::UserRole, uri);
        item->setToolTip(0, uri.toDisplayString());
        if (node.contains("children"))
            addNodes(node.value("children").toArray(), tree, item, uri);
    }
}
} // namespace

ParticipantResponseView::ParticipantResponseView(extensions::ExtensionManager* manager,
                                                 QWidget* parent)
    : QWidget(parent), manager_(manager), content_(new QVBoxLayout(this)),
      progress_(plainLabel({}, this)), error_(plainLabel({}, this)) {
    setObjectName("chatResponse");
    progress_->setObjectName("chatResponseProgress");
    error_->setObjectName("chatResponseError");
    content_->setContentsMargins(0, 0, 0, 0);
    content_->setSpacing(6);
    content_->addWidget(progress_);
    content_->addWidget(error_);
    progress_->hide();
    error_->hide();
    connect(
        manager_, &extensions::ExtensionManager::commandResult, this,
        [this](quint64 id, const QJsonValue&, const QString& error) {
            if (!commands_.remove(id) || error.isEmpty())
                return;
            error_->setText(error);
            error_->show();
        },
        Qt::QueuedConnection);
}

void ParticipantResponseView::appendPart(const QJsonObject& part) {
    if (finished_ || !extensions::ChatStreamLedger::isValidPart(part))
        return;
    const auto kind = part.value("kind").toString();
    const auto value = part.value("value");
    if (kind != "markdown") {
        markdown_.clear();
        markdownText_.clear();
    }
    QWidget* widget = nullptr;
    if (kind == "markdown") {
        const auto markdown = value.isString() ? QJsonObject{{"value", value}} : value.toObject();
        auto options = markdown;
        options.remove("value");
        if (markdown_ && markdownOptions_ == options) {
            markdownText_ += markdown.value("value").toString();
            static_cast<ResponseMarkdown*>(markdown_.data())->setResponseMarkdown(markdownText_);
            return;
        }
        auto* browser = new ResponseMarkdown(this);
        browser->setObjectName("chatResponseMarkdown");
        markdown_ = browser;
        markdownOptions_ = options;
        markdownText_ = markdown.value("value").toString();
        browser->setResponseMarkdown(markdownText_);
        connect(browser, &QTextBrowser::anchorClicked, this,
                [this, markdown](const QUrl& uri) { openLink(uri, markdown); });
        widget = browser;
    } else if (kind == "progress") {
        progress_->setText(value.toString());
        progress_->show();
        return;
    } else if (kind == "button") {
        const auto command = value.toObject();
        auto* button = new components::Button(command.value("title").toString(), this);
        button->setObjectName("chatResponseCommandButton");
        button->setToolTip(command.value("tooltip").toString());
        connect(button, &QPushButton::clicked, this, [this, command] {
            invokeCommand(command.value("command").toString(),
                          command.value("arguments").toArray());
        });
        widget = button;
    } else if (kind == "anchor" || kind == "reference") {
        const auto uri = resourceUri(value);
        const auto title = part.value("title").toString(
            uri.fileName().isEmpty() ? uri.toDisplayString() : uri.fileName());
        auto* button = new components::Button(title, this, components::ButtonVariant::Ghost);
        button->setObjectName(kind == "anchor" ? "chatResponseAnchorButton"
                                               : "chatResponseReferenceButton");
        button->setToolTip(uri.toDisplayString());
        connect(button, &QPushButton::clicked, this, [this, value] { activateResource(value); });
        widget = button;
    } else if (kind == "filetree") {
        auto* tree = new components::Tree(this);
        tree->setObjectName("chatResponseFileTree");
        tree->setHeaderHidden(true);
        addNodes(value.toArray(), tree, nullptr, QUrl(part.value("baseUri").toString()));
        tree->setMinimumHeight(96);
        connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
            if (item->childCount() == 0)
                activateResource(item->data(0, Qt::UserRole).toUrl().toString());
        });
        widget = tree;
    }
    if (widget)
        content_->insertWidget(content_->count() - 2, widget);
}

void ParticipantResponseView::finish(const QJsonObject& result, const QString& error) {
    if (finished_)
        return;
    finished_ = true;
    progress_->hide();
    const auto details = result.value("errorDetails").toObject();
    if (details.value("responseIsFiltered").toBool()) {
        // Filtered output must not remain visible or actionable.
        while (content_->count() > 2) {
            auto* item = content_->takeAt(0);
            delete item->widget();
            delete item;
        }
    }
    const auto message = error.isEmpty() ? details.value("message").toString() : error;
    error_->setText(message);
    error_->setVisible(!message.isEmpty());
}

void ParticipantResponseView::activateResource(const QJsonValue& resource) {
    const auto uri = resourceUri(resource);
    if (uri.isLocalFile()) {
        const auto start = resource.toObject().value("range").toObject().value("start").toObject();
        emit resourceActivated(uri, start.value("line").toInt() + 1,
                               start.value("character").toInt() + 1);
    } else if (uri.scheme() == "https" || uri.scheme() == "http")
        QDesktopServices::openUrl(uri);
}

void ParticipantResponseView::openLink(const QUrl& link, const QJsonObject& markdown) {
    if (link.scheme() == "command") {
        const auto command = link.path();
        bool allowed = false;
        for (const auto& id :
             markdown.value("isTrusted").toObject().value("enabledCommands").toArray())
            allowed |= id.toString() == command;
        if (!allowed)
            return;
        QJsonArray arguments;
        if (link.hasQuery()) {
            QJsonParseError error;
            const auto document =
                QJsonDocument::fromJson(link.query(QUrl::FullyDecoded).toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !document.isArray())
                return;
            arguments = document.array();
        }
        invokeCommand(command, arguments);
        return;
    }
    const auto base = QUrl(markdown.value("baseUri").toString());
    activateResource((link.isRelative() ? base.resolved(link) : link).toString());
}

void ParticipantResponseView::invokeCommand(const QString& id, const QJsonArray& arguments) {
    if (!manager_) {
        error_->setText(tr("Extension service is unavailable."));
        error_->show();
        return;
    }
    const auto request = manager_->requestCommand(id, arguments);
    if (request)
        commands_.insert(request);
    else {
        error_->setText(tr("Command is unavailable: %1").arg(id));
        error_->show();
    }
}
} // namespace litecode::ui::chat
