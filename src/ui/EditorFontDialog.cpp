#include "ui/EditorFontDialog.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QResizeEvent>

namespace litecode::ui {

EditorFontDialog::EditorFontDialog(const QFont& currentFont, const QFont& defaultFont,
                                   QWidget* parent, const QString& title)
    : QFontDialog(parent) {
    const QString dialogTitle = title.isEmpty() ? tr("Editor Font") : title;
    setObjectName(QStringLiteral("editorFontDialog"));
    setWindowTitle(dialogTitle);
    setOptions(QFontDialog::DontUseNativeDialog | QFontDialog::MonospacedFonts);
    setCurrentFont(currentFont);

    // QFontDialog uses three QListViews whose selection and hover backgrounds can
    // otherwise touch edge-to-edge. Use the view's layout spacing so the rows are
    // genuinely separated instead of trying to fake a gap in the item stylesheet.
    for (auto* list : findChildren<QListView*>(QString(), Qt::FindDirectChildrenOnly))
        list->setSpacing(1);

    // The Qt font dialog's writing-system selector is useful for document typography,
    // but not for choosing a monospaced editor or terminal font. Hide both controls
    // through their relationship instead of relying on a translated label string.
    if (auto* selector = findChild<QComboBox*>(QString(), Qt::FindDirectChildrenOnly)) {
        selector->hide();
        for (auto* label : findChildren<QLabel*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (label->buddy() == selector) {
                label->hide();
                break;
            }
        }
    }

    header_ = new DialogHeader(dialogTitle, *this, this);
    headerSeparator_ = new QFrame(this);
    headerSeparator_->setObjectName(QStringLiteral("confirmationSeparator"));
    headerSeparator_->setFixedHeight(1);
    if (QLayout* contentLayout = layout()) {
        const QMargins margins = contentLayout->contentsMargins();
        contentLayout->setContentsMargins(margins.left(), margins.top() + 37, margins.right(),
                                          margins.bottom());
    }
    header_->raise();
    headerSeparator_->raise();

    auto* buttons = findChild<QDialogButtonBox*>();
    if (buttons == nullptr) {
        DialogChrome::configure(*this);
        DialogChrome::fitToOwner(*this, QSize(760, 540), QSize(480, 360));
        return;
    }
    buttons->setObjectName(QStringLiteral("editorFontButtons"));
    if (auto* accept = buttons->button(QDialogButtonBox::Ok)) {
        accept->setText(tr("Save"));
        accept->setAccessibleName(tr("Save editor font"));
        accept->setObjectName(QStringLiteral("editorFontAcceptButton"));
        DialogChrome::markPrimary(accept);
    }
    if (auto* cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setObjectName(QStringLiteral("editorFontCancelButton"));
    }
    auto* restore = buttons->addButton(QDialogButtonBox::RestoreDefaults);
    restore->setObjectName(QStringLiteral("editorFontRestoreButton"));
    connect(this, &QFontDialog::currentFontChanged, this, [this] {
        if (!restoringDefaults_) {
            restoreDefaultsSelected_ = false;
        }
    });
    connect(restore, &QPushButton::clicked, this, [this, defaultFont] {
        restoringDefaults_ = true;
        restoreDefaultsSelected_ = true;
        setCurrentFont(defaultFont);
        restoringDefaults_ = false;
    });
    DialogChrome::configure(*this);
    DialogChrome::fitToOwner(*this, QSize(760, 540), QSize(480, 360));
}

bool EditorFontDialog::restoreDefaultsSelected() const { return restoreDefaultsSelected_; }

void EditorFontDialog::resizeEvent(QResizeEvent* event) {
    QFontDialog::resizeEvent(event);
    if (header_ != nullptr)
        header_->setGeometry(1, 1, qMax(0, width() - 2), 36);
    if (headerSeparator_ != nullptr)
        headerSeparator_->setGeometry(1, 37, qMax(0, width() - 2), 1);
}

} // namespace litecode::ui
#include <QBoxLayout>
