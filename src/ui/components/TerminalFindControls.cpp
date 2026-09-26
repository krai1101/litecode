#include "ui/components/TerminalFindControls.h"

#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

namespace litecode::ui::components {

TerminalFindControls createTerminalFindControls(QWidget* parent) {
    TerminalFindControls controls;
    controls.bar = new QFrame(parent);
    controls.bar->setObjectName(QStringLiteral("terminalFindBar"));
    controls.bar->setAccessibleName(QObject::tr("Find in terminal"));
    auto* findLayout = new QHBoxLayout(controls.bar);
    findLayout->setContentsMargins(4, 4, 4, 4);
    findLayout->setSpacing(3);
    auto* inputFrame = new QFrame(controls.bar);
    inputFrame->setObjectName(QStringLiteral("terminalFindInputFrame"));
    auto* inputLayout = new QHBoxLayout(inputFrame);
    inputLayout->setContentsMargins(0, 0, 2, 0);
    inputLayout->setSpacing(2);
    controls.input = new Input(inputFrame);
    controls.input->setObjectName(QStringLiteral("terminalFindInput"));
    controls.input->setPlaceholderText(QObject::tr("Find"));
    inputLayout->addWidget(controls.input, 1);
    findLayout->addWidget(inputFrame, 1);
    const auto addButton = [&controls, findLayout, inputFrame, inputLayout](
                               const QString& text, const QString& tooltip, bool checkable) {
        auto* button = new IconButton(checkable ? inputFrame : controls.bar);
        button->setObjectName(checkable ? QStringLiteral("terminalFindOption")
                                        : QStringLiteral("terminalFindAction"));
        button->setText(text);
        button->setToolTip(tooltip);
        button->setCheckable(checkable);
        button->setAutoRaise(true);
        if (checkable)
            inputLayout->addWidget(button);
        else
            findLayout->addWidget(button);
        return button;
    };
    const auto addActionIcon = [&addButton](const QString& icon, const QString& tooltip) {
        auto* button = addButton({}, tooltip, false);
        setThemedIcon(button, icon);
        return button;
    };
    controls.matchCase = addButton(QStringLiteral("Aa"), QObject::tr("Match Case"), true);
    controls.wholeWord = addButton(QStringLiteral("ab"), QObject::tr("Match Whole Word"), true);
    controls.regularExpression =
        addButton(QStringLiteral(".*"), QObject::tr("Use Regular Expression"), true);
    controls.countLabel = new QLabel(QStringLiteral("0 of 0"), controls.bar);
    controls.countLabel->setObjectName(QStringLiteral("terminalFindCount"));
    controls.countLabel->setFixedWidth(73);
    findLayout->addWidget(controls.countLabel);
    controls.previousButton =
        addActionIcon(QStringLiteral(":/icons/arrow-up.svg"), QObject::tr("Previous Match"));
    controls.nextButton =
        addActionIcon(QStringLiteral(":/icons/arrow-down.svg"), QObject::tr("Next Match"));
    controls.closeButton = addActionIcon(QStringLiteral(":/icons/close.svg"), QObject::tr("Close"));
    controls.bar->hide();
    return controls;
}

} // namespace litecode::ui::components
