#pragma once
#include "extensions/ProviderApi.h"
#include <QUrl>
namespace litecode::extensions {
class UriHandler {
  public:
    virtual ~UriHandler() = default;
    virtual ProviderResult<void> handleUri(const QUrl& uri) = 0;
};
} // namespace litecode::extensions
