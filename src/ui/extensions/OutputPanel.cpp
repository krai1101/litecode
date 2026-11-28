#include "ui/extensions/OutputPanel.h"
#include "extensions/WorkbenchOutput.h"
#include "ui/components/Controls.h"
#include "ui/components/IconLabelButton.h"
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QVBoxLayout>

namespace litecode::ui {
OutputPanel::OutputPanel(extensions::WorkbenchOutput& output, QWidget* parent)
    : QWidget(parent), output_(output) {
    setObjectName(QStringLiteral("extensionOutputPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    text_ = new QPlainTextEdit(this);
    text_->setObjectName(QStringLiteral("extensionOutputText"));
    text_->setAccessibleName(tr("Output"));
    text_->setReadOnly(true);
    text_->setFrameShape(QFrame::NoFrame);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    text_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    toolbar_ = new QWidget(this);
    auto* toolbar = new QHBoxLayout(toolbar_);
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(4);
    channels_ = new components::ComboBox(toolbar_);
    channels_->setObjectName("outputChannels");
    channels_->setPlaceholderText(tr("No output channels"));
    channels_->setAccessibleName(tr("Output channel"));
    channels_->setMinimumWidth(100);
    channels_->setMaximumWidth(220);
    channels_->setFixedHeight(24);
    channels_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    toolbar->addWidget(channels_);
    auto* clear = new components::IconLabelButton(toolbar_);
    clear_ = clear;
    clear->setObjectName("outputClear");
    clear->setIconLabel("$(clear-all)");
    clear->setFixedSize(24, 24);
    clear->setToolTip(tr("Clear Output"));
    clear->setAccessibleName(clear->toolTip());
    toolbar->addWidget(clear);
    layout->addWidget(toolbar_);
    layout->addWidget(text_, 1);
    connect(channels_, &QComboBox::currentIndexChanged, this, [this] { refreshSelection(); });
    connect(clear_, &QToolButton::clicked, this, [this] { output_.clear(selectedChannel()); });
    connect(&output_, &extensions::WorkbenchOutput::channelsChanged, this,
            [this] { refreshChannels(); });
    connect(&output_, &extensions::WorkbenchOutput::textChanged, this,
            [this](const QString& id, const QString& value, bool append) {
                if (id != selectedChannel())
                    return;
                if (!append) {
                    text_->setPlainText(value);
                    return;
                }
                auto* scroll = text_->verticalScrollBar();
                const bool follow = scroll->value() == scroll->maximum();
                const auto position = scroll->value();
                QTextCursor cursor(text_->document());
                cursor.movePosition(QTextCursor::End);
                cursor.insertText(value);
                scroll->setValue(follow ? scroll->maximum() : position);
            });
    refreshChannels();
}
QString OutputPanel::selectedChannel() const { return channels_->currentData().toString(); }
void OutputPanel::selectChannel(const QString& id) {
    const auto index = channels_->findData(id);
    if (index >= 0)
        channels_->setCurrentIndex(index);
}
void OutputPanel::focusOutput() { text_->setFocus(Qt::OtherFocusReason); }
void OutputPanel::setTextFont(const QFont& font) { text_->setFont(font); }
void OutputPanel::refreshChannels() {
    const auto previous = selectedChannel();
    {
        QSignalBlocker block(channels_);
        channels_->clear();
        for (const auto& channel : output_.channels())
            channels_->addItem(channel.name, channel.id);
        const auto index = channels_->findData(previous);
        if (index >= 0)
            channels_->setCurrentIndex(index);
    }
    refreshSelection();
}
void OutputPanel::refreshSelection() {
    const auto channel = output_.channel(selectedChannel());
    // Preserve selection and scroll when only channel metadata changed.
    if (text_->property("channelId").toString() != selectedChannel()) {
        text_->setProperty("channelId", selectedChannel());
        text_->setPlainText(channel ? channel->text : QString());
    }
    channels_->setEnabled(channels_->count() != 0);
    clear_->setEnabled(channel.has_value());
}
} // namespace litecode::ui
