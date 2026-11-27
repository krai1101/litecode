#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/ProviderApi.h"
#include <QFuture>
#include <QUrl>
#include <optional>

namespace litecode::extensions {
class TextDocumentContentProvider {
  public:
    virtual ~TextDocumentContentProvider() = default;
    std::optional<Event<QUrl>> onDidChange;
    virtual ProviderResult<QString> provideTextDocumentContent(const QUrl& uri, CancellationToken token) = 0;
};
} // namespace litecode::extensions
