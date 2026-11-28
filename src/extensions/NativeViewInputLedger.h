#pragma once
#include "extensions/NativeViewApi.h"
#include <QHash>
namespace litecode::extensions {
// Internal IPC bookkeeping. Public extension APIs do not expose acknowledgements.
class NativeViewInputLedger {
  public:
    quint64 record(const NativeViewEvent& event);
    bool acknowledge(quint64 sequence);
    bool pending(const QString& control) const { return pending_.contains(control); }
    void reconcile(NativeControl& root) const;

  private:
    struct Edit {
        quint64 sequence;
        NativeViewEvent event;
    };
    quint64 issued_{}, acknowledged_{};
    QHash<QString, Edit> pending_;
};
} // namespace litecode::extensions
