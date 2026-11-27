#pragma once
#include <QString>
#include <QVector>
class QScrollArea;
namespace litecode::ui {
struct NativeViewScrollState {
    int position{};
    bool followEnd{};
    QString first, last;
    QVector<QPair<QString, int>> anchors;
    static NativeViewScrollState capture(QScrollArea& scroll);
    void restore(QScrollArea& scroll) const;
};
} // namespace litecode::ui
